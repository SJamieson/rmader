#include "spline_AStar.hpp"
#include "bspline_utils.hpp"

#include <queue>
#include <vector>

#include "timer.hpp"
#include "termcolor.hpp"
using namespace termcolor;

#define WITHOUT_NUMPY  // for matplotlibcpp   TODO(move from here!)
#include "matplotlibcpp.h"

#include <random>

typedef JPS::Timer MyTimer;

namespace plt = matplotlibcpp;

SplineAStar::SplineAStar(int num_pol, int deg_pol, int num_obst, double t_min, double t_max,
                         const ConvexHullsOfCurves_Std& hulls)
{
  p_ = deg_pol;
  M_ = num_pol + 2 * p_;
  N_ = M_ - p_ - 1;
  num_of_obst_ = num_obst;
  num_of_segments_ = (M_ - 2 * p_);
  num_of_normals_ = num_of_segments_ * num_of_obst_;
  num_pol_ = num_pol;

  hulls_ = hulls;

  double deltaT = (t_max - t_min) / (1.0 * (M_ - 2 * p_ - 1 + 1));

  Eigen::RowVectorXd knots(M_ + 1);
  for (int i = 0; i <= p_; i++)
  {
    knots[i] = t_min;
  }

  for (int i = (p_ + 1); i <= M_ - p_ - 1; i++)
  {
    knots[i] = knots[i - 1] + deltaT;  // Assumming a uniform b-spline (internal knots are equally spaced)
  }

  for (int i = (M_ - p_); i <= M_; i++)
  {
    knots[i] = t_max;
  }

  knots_ = knots;
  // std::cout << "knots_=" << knots_ << std::endl;

  separator_solver_ = new separator::Separator();  // 0.0, 0.0, 0.0

  Mbs2ov_ << 182, 685, 100, -7,  //////////////////
      56, 640, 280, -16,         //////////////////
      -16, 280, 640, 56,         //////////////////
      -7, 100, 685, 182;
  Mbs2ov_ = (1.0 / 960.0) * Mbs2ov_;
  // Mbs2ov_ = Eigen::Matrix<double, 4, 4>::Identity();
  Mbs2ov_inverse_ = Mbs2ov_.inverse();

  // std::default_random_engine eng{ static_cast<long unsigned int>(time(0)) };
  // double delta = 0.0;
  // std::uniform_real_distribution<> disx(1 - delta, 1 + delta);
  // std::uniform_real_distribution<> disy(1 - delta, 1 + delta);
  // std::uniform_real_distribution<> disz(1 - delta, 1 + delta);
  // epsilons_ << disx(eng), disy(eng), disz(eng);

  // std::cout << bold << red << "EPSILONS= " << epsilons_.transpose() << reset << std::endl;

  // computeInverses();
}

SplineAStar::~SplineAStar()
{
}

int SplineAStar::getNumOfLPsRun()
{
  return num_of_LPs_run_;
}

void SplineAStar::setVisual(bool visual)
{
  visual_ = visual;
}

void SplineAStar::getBestTrajFound(trajectory& best_traj_found)
{
  trajectory traj;
  PieceWisePol pwp;
  CPs2TrajAndPwp(result_, best_traj_found, pwp, N_, p_, num_pol_, knots_, 0.1);  // Last number is the resolution
}

void SplineAStar::getAllTrajsFound(std::vector<trajectory>& all_trajs_found)
{
  all_trajs_found.clear();

  for (auto node : expanded_nodes_)
  {
    // std::cout << "using expanded_node= " << node.qi.transpose() << std::endl;

    std::vector<Eigen::Vector3d> cps;

    Node* tmp = &node;

    while (tmp != NULL)
    {
      cps.push_back(Eigen::Vector3d(tmp->qi.x(), tmp->qi.y(), tmp->qi.z()));
      tmp = tmp->previous;
    }

    cps.push_back(q1_);
    cps.push_back(q0_);  // cps = [....q4 q3 q2 q1 q0}

    std::reverse(std::begin(cps), std::end(cps));  // cps=[q0 q1 q2 q3 q4 ...]

    trajectory traj;
    PieceWisePol pwp;
    CPs2TrajAndPwp(cps, traj, pwp, N_, p_, num_pol_, knots_, 0.1);  // Last number is the resolution

    all_trajs_found.push_back(traj);
  }
}

void SplineAStar::setBasisUsedForCollision(int basis)
{
  basis_ = basis;
}

void SplineAStar::setBBoxSearch(double x, double y, double z)
{
  bbox_x_ = x;
  bbox_y_ = y;
  bbox_z_ = z;
}

void SplineAStar::setMaxValuesAndSamples(Eigen::Vector3d& v_max, Eigen::Vector3d& a_max, int num_samples_x,
                                         int num_samples_y, int num_samples_z, double fraction_voxel_size)
{
  v_max_ = v_max;
  a_max_ = a_max;

  // ensure they are odd numbers (so that vx=0 is included in the samples)
  num_samples_x_ = (num_samples_x % 2 == 0) ? ceil(num_samples_x) : num_samples_x;
  num_samples_y_ = (num_samples_y % 2 == 0) ? ceil(num_samples_y) : num_samples_y;
  num_samples_z_ = (num_samples_z % 2 == 0) ? ceil(num_samples_z) : num_samples_z;

  ////////////

  for (int i = 0; i < num_samples_x; i++)
  {
    indexes_samples_x_.push_back(i);
  }

  for (int i = 0; i < num_samples_y; i++)
  {
    indexes_samples_y_.push_back(i);
  }

  for (int i = 0; i < num_samples_z; i++)
  {
    indexes_samples_z_.push_back(i);
  }

  // std::srand(unsigned(std::time(0)));
  /*  std::srand(unsigned(std::time(0)));

    if (rand() % 2 == 1)  // o or 1
    {
      std::reverse(indexes_samples_x_.begin(), indexes_samples_x_.end());
    }

    if (rand() % 2 == 1)
    {
      std::reverse(indexes_samples_y_.begin(), indexes_samples_y_.end());
    }

    if (rand() % 2 == 1)
    {
      std::reverse(indexes_samples_z_.begin(), indexes_samples_z_.end());
    }*/

  for (int jx : indexes_samples_x_)
  {
    for (int jy : indexes_samples_y_)
    {
      for (int jz : indexes_samples_z_)
      {
        all_combinations_.push_back(std::tuple<int, int, int>(jx, jy, jz));
      }
    }
  }

  unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
  shuffle(all_combinations_.begin(), all_combinations_.end(), std::default_random_engine(seed));

  //  std::random_shuffle(all_combinations_.begin(), all_combinations_.end());

  /*  std::random_shuffle(indexes_samples_x_.begin(), indexes_samples_x_.end());
    std::random_shuffle(indexes_samples_y_.begin(), indexes_samples_y_.end());
    std::random_shuffle(indexes_samples_z_.begin(), indexes_samples_z_.end());*/
  /*
    std::cout << "indexes_samples_x_" << std::endl;
    for (auto index : indexes_samples_x_)
    {
      std::cout << index << ", " << std::endl;
    }
    std::cout << "indexes_samples_y_" << std::endl;

    for (auto index : indexes_samples_y_)
    {
      std::cout << index << ", " << std::endl;
    }

    std::cout << "indexes_samples_z_" << std::endl;
    for (auto index : indexes_samples_z_)
    {
      std::cout << index << ", " << std::endl;
    }
  */
  // TODO: remove hand-coded stuff
  // int i = 6;
  // voxel_size_ = v_max_(0) * (knots_(i + p_ + 1) - knots_(i + 1)) / (1.0 * p_);
  // voxel_size_ = fabs(vx_[1] - vx_[0]) * (knots_(i + p_ + 1) - knots_(i + 1)) / (1.0 * p_);

  double min_voxel_size;
  double max_voxel_size;
  computeLimitsVoxelSize(min_voxel_size, max_voxel_size);

  // voxel_size_ = std::max(voxel_size_, min_voxel_size);
  // voxel_size_ = std::min(voxel_size, max_voxel_size);

  // Ensure  fraction_voxel_size is in [0,1]
  fraction_voxel_size = (fraction_voxel_size > 1) ? 1 : fraction_voxel_size;
  fraction_voxel_size = (fraction_voxel_size < 0) ? 0 : fraction_voxel_size;

  voxel_size_ = min_voxel_size + fraction_voxel_size * (max_voxel_size - min_voxel_size);

  // std::cout << red << "[A*] voxel_size= " << voxel_size_ << ", limits are (" << min_voxel_size << ", " <<
  // max_voxel_size
  //           << ")" << reset << std::endl;

  // Make sure voxel_size_<= (min_voxel_size + max_voxel_size) / 2.0  (if not, very few nodes are expanded)
  // voxel_size_ = (min_voxel_size + max_voxel_size) / 2.0;

  // std::cout << "Using voxel_size_= " << voxel_size_ << std::endl;

  // note that (neighbor.qi - current.qi) is guaranteed to be an integer multiple of voxel_size_

  /*  int length_x = bbox_x_ / voxel_size_;
    int length_y = bbox_y_ / voxel_size_;
    int length_z = bbox_z_ / voxel_size_;*/

  /*  std::cout << "Allocating vector" << std::endl;
    std::vector<std::vector<std::vector<bool>>> novale(
        length_x, std::vector<std::vector<bool>>(length_y, std::vector<bool>(length_z, false)));
    std::cout << "Vector allocated" << std::endl;*/

  orig_ = q2_ - Eigen::Vector3d(bbox_x_ / 2.0, bbox_y_ / 2.0, bbox_z_ / 2.0);

  // matrixExpandedNodes_ = novale;
}

void SplineAStar::setZminZmax(double z_min, double z_max)
{
  z_min_ = z_min;
  z_max_ = z_max;
}

void SplineAStar::setBias(double bias)
{
  bias_ = bias;
}

void SplineAStar::setGoal(Eigen::Vector3d& goal)
{
  goal_ = goal;
}

void SplineAStar::setRunTime(double max_runtime)
{
  max_runtime_ = max_runtime;
}
void SplineAStar::setGoalSize(double goal_size)
{
  goal_size_ = goal_size;
}

// returns the minimum voxel_size needed (if bigger, all the neighbous from q2_ are inside the voxel of q2_)
// min_voxel_size: if voxel_size < min_voxel_size, all the neighbous from q2_ will be expanded correctly
// max_voxel_size: if voxel_size > max_voxel_size, there will be no nodes expanded from q2_ (they are on the same cell
// as q2_)

void SplineAStar::computeLimitsVoxelSize(double& min_voxel_size, double& max_voxel_size)
{
  int i = 2;
  double constraint_xL, constraint_xU, constraint_yL, constraint_yU, constraint_zL, constraint_zU;

  // std::cout << "Computing upper and lower, qiM1=" << q1_.transpose() << ", and qi=" << q2_.transpose() << std::endl;

  computeUpperAndLowerConstraints(i, q1_, q2_, constraint_xL, constraint_xU, constraint_yL, constraint_yU,
                                  constraint_zL, constraint_zU);

  /*  std::cout << "constraint_xL= " << constraint_xL << std::endl;
    std::cout << "constraint_xU= " << constraint_xU << std::endl;

    std::cout << "constraint_yL= " << constraint_yL << std::endl;
    std::cout << "constraint_yU= " << constraint_yU << std::endl;

    std::cout << "constraint_zL= " << constraint_zL << std::endl;
    std::cout << "constraint_zU= " << constraint_zU << std::endl;*/

  min_voxel_size = std::numeric_limits<double>::max();
  max_voxel_size = std::numeric_limits<double>::min();

  Eigen::Vector3d neighbor_of_q2;

  for (int jx : indexes_samples_x_)
  {
    for (int jy : indexes_samples_y_)
    {
      for (int jz : indexes_samples_z_)
      {
        // sample a velocity
        Eigen::Vector3d vi;
        vi << constraint_xL + jx * ((constraint_xU - constraint_xL) / (num_samples_x_ - 1)),  /////////
            constraint_yL + jy * ((constraint_yU - constraint_yL) / (num_samples_y_ - 1)),    /////////
            constraint_zL + jz * ((constraint_zU - constraint_zL) / (num_samples_z_ - 1));    /////////

        if (vi.norm() < 0.000001)  // remove the cases where vi is very small
        {
          continue;
        }

        //  std::cout << "vx= " << vi.x() << ", vy= " << vi.y() << ", vz= " << vi.z() << std::endl;
        //  std::cout << "(neighbor_of_q2 - q2_).norm()= " << (neighbor_of_q2 - q2_).norm() << std::endl;

        neighbor_of_q2 = (knots_(i + p_ + 1) - knots_(i + 1)) * vi / (1.0 * p_) + q2_;

        min_voxel_size = std::min(min_voxel_size, (neighbor_of_q2 - q2_).norm());
        max_voxel_size = std::max(max_voxel_size, (neighbor_of_q2 - q2_).norm());
      }
    }
  }

  /*  Eigen::Vector3d vi;

    vi << constraint_xU, constraint_yU, constraint_zU;
    Eigen::Vector3d neighbor_of_q2_U = (knots_(i + p_ + 1) - knots_(i + 1)) * vi / (1.0 * p_) + q2_;

    vi << constraint_xL, constraint_yL, constraint_zL;
    Eigen::Vector3d neighbor_of_q2_L = (knots_(i + p_ + 1) - knots_(i + 1)) * vi / (1.0 * p_) + q2_;

    max_voxel_size = std::max((neighbor_of_q2_L - q2_).norm(), (neighbor_of_q2_U - q2_).norm());*/

  /*  return 1.001 * std::max((neighbor_of_q2_L - q2_).norm(), (neighbor_of_q2_U - q2_).norm());

    return min_voxel_size;*/
}

// Compute the lower and upper bounds on the velocity based on the velocity and acceleration constraints
void SplineAStar::computeUpperAndLowerConstraints(const int i, const Eigen::Vector3d& qiM1, const Eigen::Vector3d& qi,
                                                  double& constraint_xL, double& constraint_xU, double& constraint_yL,
                                                  double& constraint_yU, double& constraint_zL, double& constraint_zU)
{
  Eigen::Vector3d viM1 = p_ * (qi - qiM1) / (knots_(i + p_) - knots_(i));  // velocity_{current.index -1}

  double tmp = (knots_(i + p_ + 1) - knots_(i + 2)) / (1.0 * (p_ - 1));

  // |vi - viM1|<=a_max_*tmp
  //  <=>
  // (vi - viM1)<=a_max_*tmp   AND   (vi - viM1)>=-a_max_*tmp
  //  <=>
  //  vi<=a_max_*tmp + viM1   AND     vi>=-a_max_*tmp + viM1
  constraint_xL = std::max(-v_max_.x(), -a_max_.x() * tmp + viM1.x());  // lower bound
  constraint_xU = std::min(v_max_.x(), a_max_.x() * tmp + viM1.x());    // upper bound

  constraint_yL = std::max(-v_max_.y(), -a_max_.y() * tmp + viM1.y());  // lower bound
  constraint_yU = std::min(v_max_.y(), a_max_.y() * tmp + viM1.y());    // upper bound

  constraint_zL = std::max(-v_max_.z(), -a_max_.z() * tmp + viM1.z());  // lower bound
  constraint_zU = std::min(v_max_.z(), a_max_.z() * tmp + viM1.z());    // upper bound

  /*  std::cout << "constraint_xL= " << constraint_xL << std::endl;
    std::cout << "constraint_xU= " << constraint_xU << std::endl;

    std::cout << "constraint_yL= " << constraint_yL << std::endl;
    std::cout << "constraint_yU= " << constraint_yU << std::endl;

    std::cout << "constraint_zL= " << constraint_zL << std::endl;
    std::cout << "constraint_zU= " << constraint_zU << std::endl;*/
}

void SplineAStar::expand(Node& current, std::vector<Node>& neighbors)
{
  MyTimer timer_expand(true);

  neighbors.clear();

  if (current.index == (N_ - 2))
  {
    // std::cout << "can't expand more in this direction" << std::endl;  // can't expand more
    return;  // neighbors = empty vector
  }

  // first expansion satisfying vmax and amax
  std::vector<Node> neighbors_va;

  int i = current.index;

  Eigen::Vector3d qiM1;

  if (current.index == 2)
  {
    qiM1 = q1_;
  }
  else
  {
    qiM1 = current.previous->qi;
  }

  double constraint_xL, constraint_xU, constraint_yL, constraint_yU, constraint_zL, constraint_zU;

  computeUpperAndLowerConstraints(i, qiM1, current.qi, constraint_xL, constraint_xU, constraint_yL, constraint_yU,
                                  constraint_zL, constraint_zU);

  //////////////////////////////

  // stuff used in the loop (here to reduce time)
  Eigen::Vector3d vi;
  Node neighbor;
  // Eigen::Vector3d aiM1;
  unsigned int ix, iy, iz;

  int jx, jy, jz;

  double delta_x = ((constraint_xU - constraint_xL) / (num_samples_x_ - 1));
  double delta_y = ((constraint_yU - constraint_yL) / (num_samples_y_ - 1));
  double delta_z = ((constraint_zU - constraint_zL) / (num_samples_z_ - 1));

  for (auto comb : all_combinations_)
  {
    jx = std::get<0>(comb);
    jy = std::get<1>(comb);
    jz = std::get<2>(comb);

    // std::cout << "using " << jx << ", " << jy << ", " << jz << std::endl;

    /*  for (int jx : indexes_samples_x_)
      {
        for (int jy : indexes_samples_y_)
        {
          for (int jz : indexes_samples_z_)
          {*/
    // sample a velocity
    vi << constraint_xL + jx * delta_x,  // ((constraint_xU - constraint_xL) / (num_samples_x_ - 1)),  /////////
        constraint_yL + jy * delta_y,    //((constraint_yU - constraint_yL) / (num_samples_y_ - 1)),    /////////
        constraint_zL + jz * delta_z;    // ((constraint_zU - constraint_zL) / (num_samples_z_ - 1));    /////////

    // std::cout << "Velocity sampled is" << vi.transpose() << std::endl;

    /*       std::cout << "constraint_zL=" << constraint_zL << std::endl;
         std::cout << "constraint_zU=" << constraint_zU << std::endl;*/

    neighbor.qi = (knots_(i + p_ + 1) - knots_(i + 1)) * vi / (1.0 * p_) + current.qi;

    neighbor.g = current.g + weightEdge(current, neighbor);
    neighbor.h = h(neighbor);

    ix = round((neighbor.qi.x() - orig_.x()) / voxel_size_);
    iy = round((neighbor.qi.y() - orig_.y()) / voxel_size_);
    iz = round((neighbor.qi.z() - orig_.z()) / voxel_size_);

    auto ptr_to_voxel = mapExpandedNodes_.find(Eigen::Vector3i(ix, iy, iz));

    bool already_exist = (ptr_to_voxel != mapExpandedNodes_.end());
    bool already_exists_with_lower_cost = false;

    if (already_exist)
    {
      // std::cout << "(*ptr_to_voxel).second" << (*ptr_to_voxel).second << std::endl;
      // std::cout << "(current.index + 1)" << (current.index + 1) << std::endl;
      already_exists_with_lower_cost = ((*ptr_to_voxel).second < (neighbor.g + bias_ * neighbor.h));
    }

    if ((vi.x() == 0 && vi.y() == 0 && vi.z() == 0) ||             // Not wanna use v=[0,0,0]
        (neighbor.qi.z() > z_max_ || neighbor.qi.z() < z_min_) ||  /// Outside the limits
        (ix >= bbox_x_ / voxel_size_ ||                            // Out. the search box
         iy >= bbox_y_ / voxel_size_ ||                            // Out. the search box
         iz >= bbox_z_ / voxel_size_) ||                           // Out. the search box
        already_exists_with_lower_cost)                            // Element already exists in the search box

    {
      continue;
    }
    else
    {  // element does not exist

      neighbor.index = current.index + 1;
      neighbor.previous = &current;
      neighbors_va.push_back(neighbor);

      mapExpandedNodes_[Eigen::Vector3i(ix, iy, iz)] = neighbor.g + bias_ * neighbor.h;
    }
    // }
    // }
    // }
  }

  // And now select the ones that satisfy the LP with all the obstacles

  std::vector<Eigen::Vector3d> last4Cps(4);

  if (current.index == 2)
  {
    last4Cps[0] = q0_;
    last4Cps[1] = q1_;
    last4Cps[2] = current.qi;
  }
  else if (current.index == 3)
  {
    last4Cps[0] = q1_;
    last4Cps[1] = current.previous->qi;
    last4Cps[2] = current.qi;
  }
  else
  {
    last4Cps[0] = current.previous->previous->qi;
    last4Cps[1] = current.previous->qi;
    last4Cps[2] = current.qi;
  }

  for (auto neighbor_va : neighbors_va)
  {
    last4Cps[3] = neighbor_va.qi;

    /*    std::cout << "====================================================================" << std::endl;

        std::cout << "[BSpline] last4Cps = " << std::endl;
        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;*/

    if (basis_ == MINVO)
    {
      transformBSpline2Minvo(last4Cps);
      // now last4CPs are in MINVO BASIS
    }

    /*    std::cout << "[basis_chosen] last4Cps = " << std::endl;
        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;*/
    /*
        /////////////////////QUITAR///////////////////////
        if (basis_ == MINVO)
        {
          transformMinvo2BSpline(last4Cps);
          // now last4CPs are in MINVO BASIS
        }

        std::cout << "[BSpline] last4Cps = " << std::endl;
        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;
        ///////////////////////////////////////////////*/

    bool satisfies_LP = true;
    // std::cout << "num_of_obst_=" << num_of_obst_ << std::endl;
    for (int obst_index = 0; obst_index < num_of_obst_; obst_index++)
    {
      Eigen::Vector3d n_i;
      double d_i;

      MyTimer timer_lp(true);

      /*      if (basis_ == MINVO)
            {
              transformBSpline2Minvo(last4Cps);
              // now last4CPs are in MINVO BASIS
            }*/

      satisfies_LP = separator_solver_->solveModel(n_i, d_i, hulls_[obst_index][current.index + 1 - 3], last4Cps);
      // transformMinvo2BSpline(last4Cps);

      /*      if (basis_ == MINVO)
            {
              transformMinvo2BSpline(last4Cps);
              // now last4CPs are in MINVO BASIS
            }*/

      /*
            std::cout << "last4Cps AFTER = " << std::endl;
            std::cout << last4Cps[0].transpose() << std::endl;
            std::cout << last4Cps[1].transpose() << std::endl;
            std::cout << last4Cps[2].transpose() << std::endl;
            std::cout << last4Cps[3].transpose() << std::endl;

            std::cout << "\nThis statisfies the LP: " << std::endl;
            std::cout << last4Cps[0].transpose() << std::endl;
            std::cout << last4Cps[1].transpose() << std::endl;
            std::cout << last4Cps[2].transpose() << std::endl;
            std::cout << last4Cps[3].transpose() << std::endl;*/

      num_of_LPs_run_++;
      time_solving_lps_ += timer_lp.ElapsedMs();

      if (satisfies_LP == false)
      {
        // std::cout << "[BSpline]" << neighbor_va.qi.transpose() << " does not satisfy constraints" << std::endl;
        break;
      }

      /*      std::cout << "Check for this obstacle (" << obst_index << ") is true" << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][0].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][1].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][2].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][3].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][4].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][5].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][6].transpose() << std::endl;
            std::cout << hulls_[obst_index][current.index + 1 - 3][7].transpose() << std::endl;*/
    }

    if (basis_ == MINVO)
    {
      transformMinvo2BSpline(last4Cps);
      // now last4CPs are in BSpline Basis (needed for the next iteration)
    }

    if (satisfies_LP == true)  // this is true also when num_of_obst_=0;
    {
      // std::cout << neighbor_va.qi.transpose() << "Satisfies the LP!" << std::endl;
      neighbors.push_back(neighbor_va);
    }
  }

  time_expanding_ += timer_expand.ElapsedMs();
}

void SplineAStar::computeInverses()
{
  Ainverses_.clear();
  Ainverses_.push_back(Eigen::MatrixXd::Zero(1, 1));  // dimension 0
  Ainverses_.push_back(Eigen::MatrixXd::Zero(1, 1));  // dimension 1
  Ainverses_.push_back(Eigen::MatrixXd::Zero(2, 2));  // dimension 2
  for (int dim = 3; dim < N_; dim++)
  {
    Eigen::MatrixXd A = 12 * Eigen::MatrixXd::Identity(dim, dim);
    Eigen::Matrix<double, -1, 1> tmp1 = -8 * Eigen::MatrixXd::Ones(dim - 1, 1);
    Eigen::Matrix<double, -1, 1> tmp2 = 2 * Eigen::MatrixXd::Ones(dim - 2, 1);
    A.diagonal(1) = tmp1;
    A.diagonal(-1) = tmp1;
    A.diagonal(2) = tmp2;
    A.diagonal(-2) = tmp2;

    Ainverses_.push_back(A.inverse());
  }
}

void SplineAStar::setq0q1q2(Eigen::Vector3d& q0, Eigen::Vector3d& q1, Eigen::Vector3d& q2)
{
  q0_ = q0;
  q1_ = q1;
  q2_ = q2;
}

double SplineAStar::h(Node& node)
{
  // See Notebook mathematica
  /*  int dim_var = N_ - node.index;

    Eigen::Matrix<double, -1, 1> b = Eigen::MatrixXd::Zero(dim_var, 1);

    double heuristics = 0;

    for (int coord = 0; coord < 3; coord++)  // separation in the three coordinates
    {
      double qi = node.qi(coord);
      double qiM1;

      if (node.index == 2)
      {
        qiM1 = q1_(coord);
      }
      else
      {
        qiM1 = node.previous->qi(coord);
      }

      double goal = goal_(coord);

      b(0, 0) = 2 * (qiM1 - 4 * qi);

      // See mathematica notebook
      if (dim_var == 3)
      {
        b(1, 0) = 2 * qi + 2 * goal;
      }
      else
      {
        b(1, 0) = 2 * qi;
        b(b.size() - 2, 0) = 2 * goal;
      }
      b(b.size() - 1, 0) = -6 * goal;
      // std::cout << "Going to compute coordinates now3" << std::endl;
      double c = qiM1 * qiM1 - 4 * qiM1 * qi + 5 * qi * qi + 2 * goal * goal;

      heuristics = heuristics + (-0.5 * b.transpose() * Ainverses_[dim_var] * b + c);
    }*/

  double heuristics = (node.qi - goal_).norm();  // hack
  // Eigen::Vector3d deltas2 = (node.qi - goal_).array().square();  //[Deltax^2  Deltay^2  Deltaz^2]''
  // double heuristics = sqrt(epsilons_.dot(deltas2));              // hack
  return heuristics;
}

double SplineAStar::g(Node& node)
{
  double cost = 0;
  Node* tmp = &node;
  while (tmp->previous != NULL)
  {
    cost = cost + weightEdge(*tmp->previous, *tmp);
    tmp = tmp->previous;
  }

  return cost;
}

double SplineAStar::weightEdge(Node& node1, Node& node2)  // edge cost when adding node 2
{
  /*  if (node1.index == 2)
    {
      return (node2.qi - 2 * node1.qi + q1_).squaredNorm();
    }
  // return (node2.qi - 2 * node1.qi + node1.previous->qi).squaredNorm();
    */

  double weight_edge = (node2.qi - node1.qi).norm();
  // Eigen::Vector3d deltas2 = (node2.qi - node1.qi).array().square();  //[Deltax^2  Deltay^2  Deltaz^2]''
  // double weight_edge = sqrt(epsilons_.dot(deltas2));                 // hack

  return weight_edge;
}

void SplineAStar::printPath(Node& node1)
{
  Node tmp = node1;
  while (tmp.previous != NULL)
  {
    // std::cout << tmp.index << ", ";  // qi.transpose().x()
    std::cout << tmp.previous->qi.transpose() << std::endl;
    tmp = *tmp.previous;
  }
  std::cout << std::endl;
}

void SplineAStar::recoverPath(Node* result_ptr)
{
  // std::cout << "Recovering path" << std::endl;
  result_.clear();

  if (result_ptr == NULL)
  {
    result_.push_back(q0_);
    result_.push_back(q1_);
    return;
  }

  Node* tmp = result_ptr;

  std::cout << "Pushing qN= " << tmp->qi.transpose() << std::endl;
  std::cout << "Pushing qN-1= " << tmp->qi.transpose() << std::endl;

  std::cout << "qi is" << std::endl;
  std::cout << tmp->qi.transpose() << std::endl;

  std::cout << "qi is" << tmp->qi.transpose() << std::endl;
  result_.push_back(tmp->qi);  // qN
  result_.push_back(tmp->qi);  // qN-1

  while (tmp != NULL)
  {
    result_.push_back(tmp->qi);
    std::cout << "pushing" << tmp->qi.transpose() << std::endl;

    tmp = tmp->previous;
  }
  std::cout << "going to push q0 and q1" << std::endl;
  result_.push_back(q1_);
  result_.push_back(q0_);

  std::cout << "reverse" << std::endl;

  std::reverse(std::begin(result_), std::end(result_));  // result_ is [q0 q1 q2 q3 ...]
}

void SplineAStar::plotExpandedNodesAndResult(std::vector<Node>& expanded_nodes, Node* result_ptr)
{
  for (auto node : expanded_nodes)
  {
    // std::cout << "using expanded_node= " << node.qi.transpose() << std::endl;

    Node* tmp = &node;

    std::vector<double> x, y, z;
    while (tmp != NULL)
    {
      x.push_back(tmp->qi.x());
      y.push_back(tmp->qi.y());
      z.push_back(tmp->qi.z());

      tmp = tmp->previous;
    }
    plt::plot(x, y, "ob-");
  }

  std::vector<std::string> colors = { "ok", "og", "oc", "om", "oy", "ok", "og", "or" };
  int counter_color = 0;
  if (result_ptr != NULL)
  {
    std::cout << "calling recoverPath1" << std::endl;
    recoverPath(result_ptr);  // saved in result_
    std::cout << "called recoverPath1" << std::endl;

    std::vector<double> x_result, y_result, z_result;

    for (auto q_i : result_)
    {
      x_result.push_back(q_i.x());
      y_result.push_back(q_i.y());
      z_result.push_back(q_i.z());
    }

    plt::plot(x_result, y_result, "or-");

    std::cout << "Path is:" << std::endl;
    for (auto q_i : result_)
    {
      std::cout << q_i.transpose() << std::endl;
    }

    if (basis_ == MINVO)  // Plot the control points using the MINVO basis
    {
      for (int i = 3; i < result_.size(); i++)
      {
        std::vector<Eigen::Vector3d> last4Cps(4);
        last4Cps[0] = result_[i - 3];
        last4Cps[1] = result_[i - 2];
        last4Cps[2] = result_[i - 1];
        last4Cps[3] = result_[i];
        std::cout << "[BSpline] Plotting these last4Cps" << std::endl;
        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;

        std::vector<double> x_result_ov, y_result_ov, z_result_ov;

        transformBSpline2Minvo(last4Cps);

        std::cout << "[MINVO]  with color=" << colors[counter_color] << std::endl;
        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;
        for (int j = 0; j < 4; j++)
        {
          x_result_ov.push_back(last4Cps[j].x());
          y_result_ov.push_back(last4Cps[j].y());
          z_result_ov.push_back(last4Cps[j].z());
        }
        plt::plot(x_result_ov, y_result_ov, colors[counter_color]);
        counter_color = counter_color + 1;
      }
    }
  }

  plt::show();
}

/*bool SplineAStar::isInExpandedList(Node& tmp)
{

}*/

void SplineAStar::transformBSpline2Minvo(std::vector<Eigen::Vector3d>& last4Cps)
{
  /////////////////////
  Eigen::Matrix<double, 4, 3> Qbs;  // b-spline
  Eigen::Matrix<double, 4, 3> Qmv;  // minvo
  Qbs.row(0) = last4Cps[0].transpose();
  Qbs.row(1) = last4Cps[1].transpose();
  Qbs.row(2) = last4Cps[2].transpose();
  Qbs.row(3) = last4Cps[3].transpose();

  /*  Eigen::Matrix<double, 4, 4> tmp;
    tmp.block(0, 0, 4, 3) = Qbs;
    tmp.block(0, 3, 4, 1) = Eigen::Matrix<double, 4, 1>::Ones();
    std::cout << "tmp BS= " << tmp << std::endl;
    std::cout << "Determinant BS=" << tmp.determinant() << std::endl;*/

  Qmv = Mbs2ov_ * Qbs;

  /*  tmp.block(0, 0, 4, 3) = Qmv;
    std::cout << "tmp OV= " << tmp << std::endl;
    std::cout << "Determinant OV=" << tmp.determinant() << std::endl;*/

  last4Cps[0] = Qmv.row(0).transpose();
  last4Cps[1] = Qmv.row(1).transpose();
  last4Cps[2] = Qmv.row(2).transpose();
  last4Cps[3] = Qmv.row(3).transpose();

  /////////////////////
}

void SplineAStar::transformMinvo2BSpline(std::vector<Eigen::Vector3d>& last4Cps)
{
  /////////////////////
  Eigen::Matrix<double, 4, 3> Qbs;  // b-spline
  Eigen::Matrix<double, 4, 3> Qmv;  // minvo
  Qmv.row(0) = last4Cps[0].transpose();
  Qmv.row(1) = last4Cps[1].transpose();
  Qmv.row(2) = last4Cps[2].transpose();
  Qmv.row(3) = last4Cps[3].transpose();

  Qbs = Mbs2ov_inverse_ * Qmv;

  last4Cps[0] = Qbs.row(0).transpose();
  last4Cps[1] = Qbs.row(1).transpose();
  last4Cps[2] = Qbs.row(2).transpose();
  last4Cps[3] = Qbs.row(3).transpose();

  /////////////////////
}

bool SplineAStar::checkFeasAndFillND(std::vector<Eigen::Vector3d>& result, std::vector<Eigen::Vector3d>& n,
                                     std::vector<double>& d)
{
  n.resize(std::max(num_of_normals_, 0), Eigen::Vector3d::Zero());
  d.resize(std::max(num_of_normals_, 0), 0.0);

  std::vector<Eigen::Vector3d> last4Cps(4);
  /*
   std::cout << "result=" << std::endl;

   for (auto result_i : result)
   {
     std::cout << result_i.transpose() << std::endl;
   }

   std::cout << "num_of_segments_= " << num_of_segments_ << std::endl;
     std::cout << "num_of_normals_= " << num_of_normals_ << std::endl;
     std::cout << "M_= " << M_ << std::endl;
     std::cout << "N_= " << N_ << std::endl;
     std::cout << "p_= " << p_ << std::endl;
   */

  for (int index_interv = 0; index_interv < (result.size() - 3); index_interv++)
  {
    last4Cps[0] = result[index_interv];
    last4Cps[1] = result[index_interv + 1];
    last4Cps[2] = result[index_interv + 2];
    last4Cps[3] = result[index_interv + 3];

    if (basis_ == MINVO)
    {
      transformBSpline2Minvo(last4Cps);
    }

    for (int obst_index = 0; obst_index < num_of_obst_; obst_index++)
    {
      Eigen::Vector3d n_i;
      double d_i;

      // transformBSpline2Minvo(last4Cps);
      bool solved = separator_solver_->solveModel(n_i, d_i, hulls_[obst_index][index_interv], last4Cps);
      //   transformMinvo2BSpline(last4Cps);

      /*      std::cout << "Filling " << obst_index * num_of_segments_ + index_interv << std::endl;
            std::cout << "OBSTACLE= " << obst_index << std::endl;
            std::cout << "INTERVAL= " << index_interv << std::endl;
            std::cout << "last4Cps= " << std::endl;
            for (auto last4Cps_i : last4Cps)
            {
              std::cout << last4Cps_i.transpose() << std::endl;
            }

            std::cout << "Obstacle= " << std::endl;
            for (auto vertex_i : hulls_[obst_index][index_interv])
            {
              std::cout << vertex_i.transpose() << std::endl;
            }
      */

      // std::cout << "index_interv= " << index_interv << std::endl;
      if (solved == false)
      {
        std::cout << "\nThis does NOT satisfy the LP: (in basis_chosen form) " << std::endl;

        std::cout << last4Cps[0].transpose() << std::endl;
        std::cout << last4Cps[1].transpose() << std::endl;
        std::cout << last4Cps[2].transpose() << std::endl;
        std::cout << last4Cps[3].transpose() << std::endl;
        /*
                std::cout << "(which, expressed in OV form, it is)" << std::endl;

                transformBSpline2Minvo(last4Cps);
                std::cout << last4Cps[0].transpose() << std::endl;
                std::cout << last4Cps[1].transpose() << std::endl;
                std::cout << last4Cps[2].transpose() << std::endl;
                std::cout << last4Cps[3].transpose() << std::endl;
                transformMinvo2BSpline(last4Cps);*/
        std::cout << bold << red << "[A*] The node provided doesn't satisfy LPs" << reset << std::endl;
        return false;
      }
      else
      {
        /*        std::cout << "\nThis satisfies the LP: (in basis_chosen form) " << std::endl;

                std::cout << last4Cps[0].transpose() << std::endl;
                std::cout << last4Cps[1].transpose() << std::endl;
                std::cout << last4Cps[2].transpose() << std::endl;
                std::cout << last4Cps[3].transpose() << std::endl;
        */
        /*        std::cout << "(which, expressed in OV form, it is)" << std::endl;

                transformBSpline2Minvo(last4Cps);
                std::cout << last4Cps[0].transpose() << std::endl;
                std::cout << last4Cps[1].transpose() << std::endl;
                std::cout << last4Cps[2].transpose() << std::endl;
                std::cout << last4Cps[3].transpose() << std::endl;
                transformMinvo2BSpline(last4Cps);*/
      }

      /*      std::cout << "solved with ni=" << n_i.transpose() << std::endl;
            std::cout << "solved with d_i=" << d_i << std::endl;*/

      n[obst_index * num_of_segments_ + index_interv] = n_i;
      d[obst_index * num_of_segments_ + index_interv] = d_i;
    }
  }
  return true;
}

bool SplineAStar::run(std::vector<Eigen::Vector3d>& result, std::vector<Eigen::Vector3d>& n, std::vector<double>& d)
{
  expanded_nodes_.clear();

  MyTimer timer_astar(true);
  // std::cout << "this->bias_ =" << this->bias_ << std::endl;
  auto cmp = [&](Node& left, Node& right) {
    return (left.g + this->bias_ * left.h) > (right.g + this->bias_ * right.h);
  };
  std::priority_queue<Node, std::vector<Node>, decltype(cmp)> openList(cmp);  //= OpenSet, = Q

  Node nodeq2;
  nodeq2.index = 0;
  nodeq2.previous = NULL;
  nodeq2.g = 0;
  nodeq2.qi = q2_;
  nodeq2.index = 2;
  nodeq2.h = h(nodeq2);  // f=g+h

  openList.push(nodeq2);

  Node* current_ptr;

  int status;

  int RUNTIME_REACHED = 0;
  int GOAL_REACHED = 1;
  int EMPTY_OPENLIST = 2;

  while (openList.size() > 0)
  {
    // std::cout << red << "=============================" << reset << std::endl;

    current_ptr = new Node;
    *current_ptr = openList.top();

    // std::cout << "Path to current_ptr= " << (*current_ptr).qi.transpose() << std::endl;
    // printPath(*current_ptr);

    double dist = ((*current_ptr).qi - goal_).norm();
    // std::cout << "dist2Goal=" << dist << ", index=" << (*current_ptr).index << std::endl;
    // std::cout << "bias_=" << bias_ << std::endl;
    // std::cout << "N_-2=" << N_ - 2 << std::endl;
    // std::cout << "index=" << (*current_ptr).index << std::endl;

    // log the closest solution so far
    // if ((*current_ptr).index == (N_ - 2))
    // {
    if (dist < closest_dist_so_far_)
    {
      closest_dist_so_far_ = dist;
      closest_result_so_far_ptr_ = current_ptr;
    }
    // }

    // std::cout << "time_solving_lps_= " << 100 * time_solving_lps_ / (timer_astar.ElapsedMs()) << "%" << std::endl;
    // std::cout << "time_expanding_= " << time_expanding_ << "ms, " << 100 * time_expanding_ /
    // (timer_astar.ElapsedMs())
    //           << "%" << std::endl;

    //////////////////////////////////////////////////////////////////
    //////////////Check if we are in the goal or if the runtime is over

    if (timer_astar.ElapsedMs() > (max_runtime_ * 1000))
    {
      status = RUNTIME_REACHED;
      goto exitloop;
    }

    // check if we are already in the goal
    if ((dist < goal_size_) && (*current_ptr).index == (N_ - 2))
    {
      status = GOAL_REACHED;
      goto exitloop;
    }
    ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////

    openList.pop();  // remove the element

    std::vector<Node> neighbors;
    expand(*current_ptr, neighbors);
    expanded_nodes_.push_back((*current_ptr));

    for (auto neighbor : neighbors)
    {
      openList.push(neighbor);
    }
  }

  status = EMPTY_OPENLIST;
  goto exitloop;

exitloop:

  Node* best_node_ptr = NULL;

  if (status == GOAL_REACHED)
  {
    std::cout << "[A*] Goal was reached!" << std::endl;
    best_node_ptr = current_ptr;
  }
  else if (status == RUNTIME_REACHED)
  {
    std::cout << "[A*] Max Runtime was reached" << std::endl;
    best_node_ptr = closest_result_so_far_ptr_;
  }
  else
  {
    std::cout << "[A*] openList is empty" << std::endl;
    return false;
  }

  // Fill (until we arrive to N_-2), with the same qi
  // Note that, by doing this, it's not guaranteed feasibility wrt a dynamic obstacle
  // and hence the need of the function checkFeasAndFillND()
  for (int j = (best_node_ptr->index) + 1; j <= N_ - 2; j++)
  {
    Node* node_ptr = new Node;
    node_ptr->qi = best_node_ptr->qi;
    node_ptr->index = j;
    std::cout << "Filled " << j << ", ";
    // << node_ptr->qi.transpose() << std::endl;
    node_ptr->previous = best_node_ptr;
    best_node_ptr = node_ptr;
  }

  std::cout << "calling recoverPath3" << std::endl;
  recoverPath(best_node_ptr);  // saved in result_
  std::cout << "called recoverPath3" << std::endl;

  if (visual_)
  {
    plotExpandedNodesAndResult(expanded_nodes_, best_node_ptr);
  }

  if (checkFeasAndFillND(result_, n, d) == false)  // This may be true or false, depending on the case
  {
    return false;
  }
  else
  {
    return true;
  }

  /*
    return false;

    switch (status)
    {
      case GOAL_REACHED:
        recoverPath(current_ptr);                       // saved in result_
        if (checkFeasAndFillND(result, n, d) == false)  // This should always be true
        {
          return false;
        }
        if (visual_)
        {
          // plotExpandedNodesAndResult(expanded_nodes_, current_ptr);
        }
        return true;

      case RUNTIME_REACHED:
    }

    if (closest_result_so_far_ptr_ == NULL || (closest_result_so_far_ptr_->index == 2))
    {
      std::cout << " and couldn't find any solution" << std::endl;
      if (visual_)
      {
        plotExpandedNodesAndResult(expanded_nodes_, closest_result_so_far_ptr_);
      }
      return false;
    }
    else
    {
      // Fill the until we arrive to N_-2, with the same qi
      // Note that, by doing this, it's not guaranteed feasibility wrt a dynamic obstacle
      // and hence the need of the function checkFeasAndFillND()
      for (int j = (closest_result_so_far_ptr_->index) + 1; j <= N_ - 2; j++)
      {
        Node* node_ptr = new Node;
        node_ptr->qi = closest_result_so_far_ptr_->qi;
        node_ptr->index = j;
        std::cout << "Filled " << j << ", ";
        // << node_ptr->qi.transpose() << std::endl;
        node_ptr->previous = closest_result_so_far_ptr_;
        closest_result_so_far_ptr_ = node_ptr;
      }
      std::cout << std::endl;

      // std::cout << " best solution has dist=" << closest_dist_so_far_ << std::endl;

      std::cout << "calling recoverPath3" << std::endl;
      recoverPath(closest_result_so_far_ptr_);  // saved in result_
      std::cout << "called recoverPath3" << std::endl;

      if (visual_)
      {
        plotExpandedNodesAndResult(expanded_nodes_, closest_result_so_far_ptr_);
      }

      if (checkFeasAndFillND(result_, n, d) == false)  // This may be true or false, depending on the case
      {
        return false;
      }

      return true;
    }

    return false;*/
}

/*          std::cout << "ix= " << ix << " is outside, max= " << matrixExpandedNodes_.size() << std::endl;
          std::cout << "iy= " << iy << " is outside, max= " << matrixExpandedNodes_[0].size() << std::endl;
          std::cout << "iz= " << iz << " is outside, max= " << matrixExpandedNodes_[0][0].size() << std::endl;
          std::cout << "voxel_size_= " << voxel_size_ << std::endl;
          std::cout << "orig_= " << orig_.transpose() << std::endl;
          std::cout << "neighbor.qi= " << neighbor.qi.transpose() << std::endl;*/

/*  vx_.clear();
  vy_.clear();
  vz_.clear();

  for (int i = 0; i < num_samples_x_; i++)
  {
    vx_.push_back(-v_max_.x() + i * ((2.0 * v_max_.x()) / (num_samples_x_ - 1)));
  }
  for (int i = 0; i < num_samples_y_; i++)
  {
    vy_.push_back(-v_max_.y() + i * ((2.0 * v_max_.y()) / (num_samples_y_ - 1)));
  }
  for (int i = 0; i < num_samples_z_; i++)
  {
    vz_.push_back(-v_max_.z() + i * ((2.0 * v_max_.z()) / (num_samples_z_ - 1)));
  }

  std::cout << "vx is: " << std::endl;
  for (auto vxi : vx_)
  {
    std::cout << "vxi= " << vxi << std::endl;
  }
*/

/*  vx_.clear();
  vy_.clear();
  vz_.clear();

  for (int i = 0; i < num_samples_x_; i++)
  {
    vx_.push_back(constraint_xL + i * ((constraint_xU - constraint_xL) / (num_samples_x_ - 1)));
  }
  for (int i = 0; i < num_samples_y_; i++)
  {
    vy_.push_back(constraint_yL + i * ((constraint_yU - constraint_yL) / (num_samples_y_ - 1)));
  }
  for (int i = 0; i < num_samples_z_; i++)
  {
    vz_.push_back(constraint_zL + i * ((constraint_zU - constraint_zL) / (num_samples_z_ - 1)));
  }*/

/*  std::cout << "vx is: " << std::endl;
  for (auto tmp_i : vx_)
  {
    std::cout << tmp_i << std::endl;
  }
*/
// std::cout << "vx_i=" << vx_i << ", vy_i=" << vy_i << ", vz_i=" << vz_i << std::endl;
//    if (constraint_zL <= vz_i <= constraint_zU)
//    {
// std::cout << "vx_i=" << vx_i << ", vy_i=" << vy_i << ", vz_i=" << vz_i << std::endl;

/*  std::cout << "Current= " << current.qi.transpose() << std::endl;

  std::cout << "Current index= " << current.index << std::endl;
  std::cout << "knots= " << knots_ << std::endl;
  std::cout << "tmp= " << tmp << std::endl;
  std::cout << "a_max_.x() * tmp= " << a_max_.x() * tmp << std::endl;
  std::cout << "v_iM1.x()= " << v_iM1.x() << std::endl;*/

/*  vx_.clear();
  vy_.clear();
  vz_.clear();

  vx_.push_back(v_max_(0));
  vx_.push_back(v_max_(0) / 2.0);
  vx_.push_back(0);
  vx_.push_back(-v_max_(0) / 2.0);
  vx_.push_back(-v_max_(0));

  vy_.push_back(v_max_(1));
  vy_.push_back(v_max_(1) / 2.0);
  vy_.push_back(0);
  vy_.push_back(-v_max_(1) / 2.0);
  vy_.push_back(-v_max_(1));

  vz_.push_back(v_max_(2));
  vz_.push_back(v_max_(2) / 2.0);
  vz_.push_back(0);
  vz_.push_back(-v_max_(2) / 2.0);
  vz_.push_back(-v_max_(2));*/

/*  Eigen::Matrix<double, 4, 1> tmp_x;
  tmp_x << v_max_.x(), -v_max_.x(), a_max_.x() * tmp + viM1.x(), -a_max_.x() * tmp + viM1.x();
  double constraint_xU = tmp_x.maxCoeff();  // upper bound
  double constraint_xL = tmp_x.minCoeff();  // lower bound

  Eigen::Matrix<double, 4, 1> tmp_y;
  tmp_y << v_max_.y(), -v_max_.y(), a_max_.y() * tmp + viM1.y(), -a_max_.y() * tmp + viM1.y();
  double constraint_yU = tmp_y.maxCoeff();  // upper bound
  double constraint_yL = tmp_y.minCoeff();  // lower bound

  Eigen::Matrix<double, 4, 1> tmp_z;
  tmp_z << v_max_.z(), -v_max_.z(), a_max_.z() * tmp + viM1.z(), -a_max_.z() * tmp + viM1.z();
  double constraint_zU = tmp_z.maxCoeff();  // upper bound
  double constraint_zL = tmp_z.minCoeff();  // lower bound*/

/*  std::cout << "constraint_xL= " << constraint_xL << std::endl;
  std::cout << "constraint_xU= " << constraint_xU << std::endl;
  std::cout << "constraint_yL= " << constraint_yL << std::endl;
  std::cout << "constraint_yU= " << constraint_yU << std::endl;
  std::cout << "constraint_zL= " << constraint_zL << std::endl;
  std::cout << "constraint_zU= " << constraint_zU << std::endl;*/

/*    std::cout << "======================" << std::endl;

    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> novale = openList;

    while (!novale.empty())
    {
      std::cout << (novale.top().h + novale.top().g) << " ";
      novale.pop();
    }*/

/*      double tentative_g = current.g + weightEdge(current, neighbor);  // Cost to come + new edge
      std::cout << "TEST" << std::endl;
      std::cout << "tentative_g=" << tentative_g << std::endl;
      std::cout << "g(neighbor)=" << g(neighbor) << std::endl;
      std::cout << "neighor.index=" << neighbor.index << std::endl;

      if (tentative_g < g(neighbor))
      {
        neighbor.previous = &current;
        neighbor.g = tentative_g;
        neighbor.f = neighbor.g + h(neighbor);
        // if neighbor not in OPEN SET TODO!!
        openList.push(neighbor);  // the order is automatic (it's a prioriry queue)
      }*/

/*  std::cout << "tmp=" << tmp << std::endl;

  std::cout << "vx_ =" << std::endl;

  for (double vx_i : vx_)
  {
    std::cout << vx_i << ", ";
  }
  std::cout << std::endl;

  for (double vy_i : vy_)
  {
    std::cout << vy_i << ", ";
  }
  std::cout << std::endl;

  for (double vz_i : vz_)
  {
    std::cout << vz_i << ", ";
  }
  std::cout << std::endl;*/

/*  if (current.index == 0)
  {
    Node nodeq1 = current;
    nodeq1.index = 1;
    nodeq1.previous = &current;
    nodeq1.g = 0;
    nodeq1.f = nodeq1.g + h(nodeq1);  // f=g+h

    neighbors.push_back(nodeq1);
    return neighbors;
  }

  if (current.index == 1)
  {
    Node nodeq2 = current;
    nodeq2.index = 1;
    nodeq2.previous = &current;
    nodeq2.g = current.previous->g + weightEdge(current, nodeq2);
    nodeq2.f = nodeq2.g + h(nodeq2);  // f=g+h

    neighbors.push_back(nodeq2);
    return neighbors;
  }*/

/*      std::cout << "Size cvxhull=" << hulls_[obst_index][current.index + 1 - 3].size() << std::endl;

      std::cout << "Vertexes obstacle" << std::endl;
      for (auto vertex_obs : hulls_[obst_index][current.index + 1 - 3])
      {
        std::cout << "vertex_obs= " << vertex_obs.transpose() << std::endl;
      }

      std::cout << "CPs" << std::endl;
      for (auto cp : last4Cps)
      {
        std::cout << "cp= " << cp.transpose() << std::endl;
      }

      std::cout << "n_i= " << n_i.transpose() << ", d_i= " << d_i << std::endl;*/