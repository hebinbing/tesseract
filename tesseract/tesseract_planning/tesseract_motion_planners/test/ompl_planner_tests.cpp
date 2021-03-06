/**
 * @file ompl_planner_tests.cpp
 * @brief This contains unit test for the tesseract descartes planner
 *
 * @author Levi Armstrong, Jonathan Meyer
 * @date September 16, 2019
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2019, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <tesseract_common/macros.h>
TESSERACT_COMMON_IGNORE_WARNINGS_PUSH
#include <boost/filesystem/path.hpp>

#include <ompl/geometric/planners/sbl/SBL.h>
#include <ompl/geometric/planners/est/EST.h>
#include <ompl/geometric/planners/kpiece/LBKPIECE1.h>
#include <ompl/geometric/planners/kpiece/BKPIECE1.h>
#include <ompl/geometric/planners/kpiece/KPIECE1.h>
#include <ompl/geometric/planners/rrt/RRT.h>
#include <ompl/geometric/planners/rrt/RRTConnect.h>
#include <ompl/geometric/planners/rrt/RRTstar.h>
#include <ompl/geometric/planners/rrt/TRRT.h>
#include <ompl/geometric/planners/prm/PRM.h>
#include <ompl/geometric/planners/prm/PRMstar.h>
#include <ompl/geometric/planners/prm/LazyPRMstar.h>
#include <ompl/geometric/planners/prm/SPARS.h>

#include <ompl/util/RandomNumbers.h>

#include <functional>
#include <cmath>
#include <gtest/gtest.h>
TESSERACT_COMMON_IGNORE_WARNINGS_POP

#include <tesseract/tesseract.h>
#include <tesseract_environment/core/utils.h>
#include <tesseract_motion_planners/ompl/ompl_motion_planner.h>
#include <tesseract_motion_planners/ompl/ompl_planner_configurator.h>
#include <tesseract_motion_planners/ompl/config/ompl_planner_freespace_config.h>

using namespace tesseract;
using namespace tesseract_scene_graph;
using namespace tesseract_collision;
using namespace tesseract_environment;
using namespace tesseract_geometry;
using namespace tesseract_kinematics;
using namespace tesseract_motion_planners;

const static int SEED = 1;
const static std::vector<double> start_state = { -0.5, 0.5, 0.0, -1.3348, 0.0, 1.4959, 0.0 };
const static std::vector<double> end_state = { 0.5, 0.5, 0.0, -1.3348, 0.0, 1.4959, 0.0 };

std::string locateResource(const std::string& url)
{
  std::string mod_url = url;
  if (url.find("package://tesseract_support") == 0)
  {
    mod_url.erase(0, strlen("package://tesseract_support"));
    size_t pos = mod_url.find('/');
    if (pos == std::string::npos)
    {
      return std::string();
    }

    std::string package = mod_url.substr(0, pos);
    mod_url.erase(0, pos);
    std::string package_path = std::string(TESSERACT_SUPPORT_DIR);

    if (package_path.empty())
    {
      return std::string();
    }

    mod_url = package_path + mod_url;
  }

  return mod_url;
}

static void addBox(tesseract_environment::Environment& env)
{
  Link link_1("box_attached");

  Visual::Ptr visual = std::make_shared<Visual>();
  visual->origin = Eigen::Isometry3d::Identity();
  visual->origin.translation() = Eigen::Vector3d(0.5, 0, 0.55);
  visual->geometry = std::make_shared<tesseract_geometry::Box>(0.4, 0.001, 0.4);
  link_1.visual.push_back(visual);

  Collision::Ptr collision = std::make_shared<Collision>();
  collision->origin = visual->origin;
  collision->geometry = visual->geometry;
  link_1.collision.push_back(collision);

  Joint joint_1("joint_n1");
  joint_1.parent_link_name = "base_link";
  joint_1.child_link_name = link_1.getName();
  joint_1.type = JointType::FIXED;

  env.addLink(std::move(link_1), std::move(joint_1));
}

template <typename Configurator>
class OMPLTestFixture : public ::testing::Test
{
public:
  OMPLTestFixture() { configurator = std::make_shared<Configurator>(); }
  using ::testing::Test::Test;
  std::shared_ptr<Configurator> configurator;
  tesseract_motion_planners::OMPLMotionPlanner ompl_planner;
};

using Implementations = ::testing::Types<tesseract_motion_planners::SBLConfigurator,
                                         tesseract_motion_planners::PRMConfigurator,
                                         tesseract_motion_planners::PRMstarConfigurator,
                                         tesseract_motion_planners::LazyPRMstarConfigurator,
                                         tesseract_motion_planners::ESTConfigurator,
                                         tesseract_motion_planners::LBKPIECE1Configurator,
                                         tesseract_motion_planners::BKPIECE1Configurator,
                                         tesseract_motion_planners::KPIECE1Configurator,
                                         // tesseract_motion_planners::RRTConfigurator,
                                         // tesseract_motion_planners::RRTstarConfigurator,
                                         // tesseract_motion_planners::SPARSConfigurator,
                                         // tesseract_motion_planners::TRRTConfigurator,
                                         tesseract_motion_planners::RRTConnectConfigurator>;

TYPED_TEST_CASE(OMPLTestFixture, Implementations);

TYPED_TEST(OMPLTestFixture, OMPLFreespacePlannerUnit)
{
  using namespace tesseract_motion_planners;
  EXPECT_EQ(ompl::RNG::getSeed(), SEED) << "Randomization seed does not match expected: " << ompl::RNG::getSeed()
                                        << " vs. " << SEED;

  // Step 1: Load scene and srdf
  tesseract_scene_graph::ResourceLocator::Ptr locator =
      std::make_shared<tesseract_scene_graph::SimpleResourceLocator>(locateResource);
  Tesseract::Ptr tesseract = std::make_shared<Tesseract>();
  boost::filesystem::path urdf_path(std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.urdf");
  boost::filesystem::path srdf_path(std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.srdf");
  EXPECT_TRUE(tesseract->init(urdf_path, srdf_path, locator));

  // Step 2: Add box to environment
  addBox(*(tesseract->getEnvironment()));

  // Step 3: Create ompl planner config and populate it
  auto kin = tesseract->getFwdKinematicsManagerConst()->getFwdKinematicSolver("manipulator");
  std::vector<double> swp = start_state;
  std::vector<double> ewp = end_state;

  // This will create two planners of the same type and run them in different threads
  std::vector<OMPLPlannerConfigurator::ConstPtr> planners = { this->configurator, this->configurator };
  auto ompl_config = std::make_shared<OMPLPlannerFreespaceConfig>(tesseract, "manipulator", planners);

  ompl_config->start_waypoint = std::make_shared<JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<JointWaypoint>(ewp, kin->getJointNames());
  ompl_config->collision_safety_margin = 0.02;
  ompl_config->planning_time = 5.0;
  ompl_config->max_solutions = 2;
  ompl_config->longest_valid_segment_fraction = 0.01;

  ompl_config->collision_continuous = true;
  ompl_config->collision_check = true;
  ompl_config->simplify = false;
  ompl_config->n_output_states = 50;

  // Set the planner configuration
  this->ompl_planner.setConfiguration(ompl_config);

  tesseract_motion_planners::PlannerResponse ompl_planning_response;
  tesseract_common::StatusCode status = this->ompl_planner.solve(ompl_planning_response);

  if (!status)
  {
    CONSOLE_BRIDGE_logError("CI Error: %s", status.message().c_str());
  }
  EXPECT_TRUE(&status);
  EXPECT_EQ(ompl_planning_response.joint_trajectory.trajectory.rows(), ompl_config->n_output_states);
  EXPECT_TRUE(Eigen::Map<Eigen::VectorXd>(swp.data(), static_cast<Eigen::Index>(swp.size()))
                  .transpose()
                  .isApprox(ompl_planning_response.joint_trajectory.trajectory.row(0), 1e-5));
  EXPECT_TRUE(Eigen::Map<Eigen::VectorXd>(ewp.data(), static_cast<Eigen::Index>(ewp.size()))
                  .transpose()
                  .isApprox(ompl_planning_response.joint_trajectory.trajectory.bottomRows(1), 1e-5));

  // Check for start state in collision error
  swp = { 0, 0.7, 0.0, 0, 0.0, 0, 0.0 };
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());

  this->ompl_planner.setConfiguration(ompl_config);
  status = this->ompl_planner.solve(ompl_planning_response);

  EXPECT_FALSE(status);

  // Check for start state in collision error
  swp = start_state;
  ewp = { 0, 0.7, 0.0, 0, 0.0, 0, 0.0 };
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(ewp, kin->getJointNames());

  this->ompl_planner.setConfiguration(ompl_config);
  status = this->ompl_planner.solve(ompl_planning_response);

  EXPECT_FALSE(status);

  // Reset start and end waypoints
  swp = start_state;
  ewp = end_state;
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(ewp, kin->getJointNames());
}

TEST(OMPLMultiPlanner, OMPLMultiPlannerUnit)  // NOLINT
{
  using namespace tesseract_motion_planners;
  EXPECT_EQ(ompl::RNG::getSeed(), SEED) << "Randomization seed does not match expected: " << ompl::RNG::getSeed()
                                        << " vs. " << SEED;

  // Step 1: Load scene and srdf
  tesseract_scene_graph::ResourceLocator::Ptr locator =
      std::make_shared<tesseract_scene_graph::SimpleResourceLocator>(locateResource);
  Tesseract::Ptr tesseract = std::make_shared<Tesseract>();
  boost::filesystem::path urdf_path(std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.urdf");
  boost::filesystem::path srdf_path(std::string(TESSERACT_SUPPORT_DIR) + "/urdf/lbr_iiwa_14_r820.srdf");
  EXPECT_TRUE(tesseract->init(urdf_path, srdf_path, locator));

  // Step 2: Add box to environment
  addBox(*(tesseract->getEnvironment()));

  // Step 3: Create ompl planner config and populate it
  auto kin = tesseract->getFwdKinematicsManagerConst()->getFwdKinematicSolver("manipulator");
  std::vector<double> swp = start_state;
  std::vector<double> ewp = end_state;

  tesseract_motion_planners::OMPLMotionPlanner ompl_planner;

  std::vector<OMPLPlannerConfigurator::ConstPtr> planners = { std::make_shared<SBLConfigurator>(),
                                                              std::make_shared<RRTConnectConfigurator>() };
  auto ompl_config = std::make_shared<OMPLPlannerFreespaceConfig>(tesseract, "manipulator", planners);

  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(ewp, kin->getJointNames());
  ompl_config->collision_safety_margin = 0.02;
  ompl_config->planning_time = 5.0;
  ompl_config->max_solutions = 2;
  ompl_config->longest_valid_segment_fraction = 0.01;

  ompl_config->collision_continuous = true;
  ompl_config->collision_check = true;
  ompl_config->simplify = false;
  ompl_config->n_output_states = 50;

  // Set the planner configuration
  ompl_planner.setConfiguration(ompl_config);

  tesseract_motion_planners::PlannerResponse ompl_planning_response;
  tesseract_common::StatusCode status = ompl_planner.solve(ompl_planning_response);

  if (!status)
  {
    CONSOLE_BRIDGE_logError("CI Error: %s", status.message().c_str());
  }
  EXPECT_TRUE(&status);
  EXPECT_EQ(ompl_planning_response.joint_trajectory.trajectory.rows(), ompl_config->n_output_states);

  // Check for start state in collision error
  swp = { 0, 0.7, 0.0, 0, 0.0, 0, 0.0 };
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());

  ompl_planner.setConfiguration(ompl_config);
  status = ompl_planner.solve(ompl_planning_response);

  EXPECT_FALSE(status);

  // Check for start state in collision error
  swp = start_state;
  ewp = { 0, 0.7, 0.0, 0, 0.0, 0, 0.0 };
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(ewp, kin->getJointNames());

  ompl_planner.setConfiguration(ompl_config);
  status = ompl_planner.solve(ompl_planning_response);

  EXPECT_FALSE(status);

  // Reset start and end waypoints
  swp = start_state;
  ewp = end_state;
  ompl_config->start_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(swp, kin->getJointNames());
  ompl_config->end_waypoint = std::make_shared<tesseract_motion_planners::JointWaypoint>(ewp, kin->getJointNames());
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  // Set the randomization seed for the planners to get repeatable results
  ompl::RNG::setSeed(SEED);

  return RUN_ALL_TESTS();
}
