#include <catch2/catch.hpp>
#include <wdmcpl/point_search.h>
#include <Omega_h_mesh.hpp>
#include <Omega_h_build.hpp>
#include <Kokkos_Core.hpp>

using wdmcpl::AABBox;
using wdmcpl::barycentric_from_global;
using wdmcpl::UniformGrid;

extern Omega_h::Library* omega_h_library;

TEST_CASE("global to local")
{
  Omega_h::Matrix<2, 3> coords{{0.0}, {1, 0}, {0.5, 1}};
  SECTION("check verts")
  {
    for (int i = 0; i < coords.size(); ++i) {
      auto xi = barycentric_from_global(coords[i], coords);
      Omega_h::Vector<3> hand_xi{0, 0, 0};
      hand_xi[i] = 1;
      printf("[%f,%f,%f] == [%f,%f,%f]\n", xi[0], xi[1], xi[2], hand_xi[0],
             hand_xi[1], hand_xi[2]);
      REQUIRE(Omega_h::are_close(xi, hand_xi));
    }
  }
  SECTION("check point")
  {
    Omega_h::Vector<2> point{0.5, 0.5};
    auto xi = barycentric_from_global(point, coords);
    Omega_h::Vector<3> hand_xi{0.25, 0.25, 0.5};
    printf("[%f,%f,%f] == [%f,%f,%f]\n", xi[0], xi[1], xi[2], hand_xi[0],
           hand_xi[1], hand_xi[2]);
    REQUIRE(are_close(xi, hand_xi));
  }
}
TEST_CASE("Triangle AABB intersection")
{
  using wdmcpl::triangle_intersects_bbox;
  AABBox<2> unit_square{.center = {0, 0}, .half_width = {0.5, 0.5}};
  SECTION("Triangle outside bbox")
  {
    REQUIRE(triangle_intersects_bbox({{1, 1}, {2, 1}, {2, 2}}, unit_square) ==
            false);
  }
  SECTION("BBOX inside Triangle")
  {
    REQUIRE(triangle_intersects_bbox({{-10, -10}, {10, -10}, {10, 10}},
                                     unit_square) == true);
  }
  SECTION("All verts inside BBOX")
  {
    REQUIRE(triangle_intersects_bbox({{-10, -10}, {10, -10}, {10, 10}},
                                     unit_square) == true);
  }
  SECTION("Single vert inside bbox")
  {
    REQUIRE(triangle_intersects_bbox({{0, 0}, {1, 0}, {1, 1}}, unit_square) ==
            true);
    REQUIRE(triangle_intersects_bbox({{1, 1}, {0, 0}, {1, 0}}, unit_square) ==
            true);
    REQUIRE(triangle_intersects_bbox({{1, 0}, {1, 1}, {0, 0}}, unit_square) ==
            true);
  }
  SECTION("edge crossed bbox")
  {
    // Triangle 1 with all vert permutations
    REQUIRE(triangle_intersects_bbox({{-0.6, 0}, {0, 0.6}, {-1, 1}},
                                     unit_square) == true);
    REQUIRE(triangle_intersects_bbox({{-1, 1}, {-0.6, 0}, {0, 0.6}},
                                     unit_square) == true);
    REQUIRE(triangle_intersects_bbox({{0, 0.6}, {-1, 1}, {-0.6, 0}},
                                     unit_square) == true);
    // triangle 2 with all vert permutations
    REQUIRE(triangle_intersects_bbox({{-0.6, 0}, {0.6, 0}, {0, -1}},
                                     unit_square) == true);
    REQUIRE(triangle_intersects_bbox({{0, -1}, {-0.6, 0}, {0.6, 0}},
                                     unit_square) == true);
    REQUIRE(triangle_intersects_bbox({{0.6, 0}, {0, -1}, {-0.6, 0}},
                                     unit_square) == true);
  }
}
TEST_CASE("Triangle BBox Intersection Regression")
{
  using wdmcpl::triangle_intersects_bbox;
  AABBox<2> bbox{.center = {0.10833333, 0.10833333},
                 .half_width = {0.00833333, 0.00833333}};
  REQUIRE(triangle_intersects_bbox({{0, 0}, {0.1, 0}, {0.1, 0.1}}, bbox) ==
          true);
  REQUIRE(triangle_intersects_bbox({{0.1, 0}, {0.2, 0}, {0.2, 0.1}}, bbox) ==
          false);
  REQUIRE(triangle_intersects_bbox({{0.1, 0.1}, {0, 0.1}, {0, 0}}, bbox) ==
          true);
  REQUIRE(triangle_intersects_bbox({{0.1, 0.1}, {0.2, 0.1}, {0.2, 0.2}},
                                   bbox) == true);
  REQUIRE(triangle_intersects_bbox({{0, 0.1}, {0.1, 0.1}, {0.1, 0.2}}, bbox) ==
          true);
  REQUIRE(triangle_intersects_bbox({{0.1, 0.2}, {0, 0.2}, {0, 0.1}}, bbox) ==
          false);
  REQUIRE(triangle_intersects_bbox({{0.2, 0.2}, {0.1, 0.2}, {0.1, 0.1}},
                                   bbox) == true);
  REQUIRE(triangle_intersects_bbox({{0.2, 0.1}, {0.1, 0.1}, {0.1, 0}}, bbox) ==
          true);
}

template <typename... T> struct dependent_always_false : std::false_type {};

template <typename T>
void print_template_type() {
  static_assert(dependent_always_false<T>::value);
}

template <typename T>
bool num_candidates_within_range(const T& intersection_map, wdmcpl::LO min,
                                 wdmcpl::LO max)
{
  using size_type = typename T::size_type;
  using reducer_type = Kokkos::MinMax<double, Kokkos::HostSpace>;
  using result_type = typename reducer_type::value_type;
  result_type result;
  Kokkos::parallel_reduce("Reduction",
    intersection_map.numRows(),
    KOKKOS_LAMBDA(const int i, result_type& update) {
      auto num_candidates = intersection_map.row_map(i + 1) - intersection_map.row_map(i);
      if (num_candidates > update.max_val)
        update.max_val = num_candidates;
      if (num_candidates < update.min_val)
        update.min_val = num_candidates;
    },
    reducer_type(result));
  std::cout<<result.min_val<<" "<<result.max_val<<"\n";
  return (result.min_val >= min) && (result.max_val <= max);
}

TEST_CASE("construct intersection map")
{
  auto* lib = omega_h_library;
  auto world = lib->world();
  auto mesh =
    Omega_h::build_box(world, OMEGA_H_SIMPLEX, 1, 1, 1, 10, 10, 0, false);
  REQUIRE(mesh.dim() == 2);
  SECTION("grid bbox overlap")
  {
    UniformGrid grid{
      .edge_length{1, 1}, .bot_left = {0, 0}, .divisions = {10, 10}};
    auto intersection_map = wdmcpl::detail::construct_intersection_map(mesh, grid);
    REQUIRE(intersection_map.numRows() == 100);
    REQUIRE(num_candidates_within_range(intersection_map, 2, 16));
  }
   SECTION("fine grid")
  {
     UniformGrid grid{
       .edge_length{1, 1}, .bot_left = {0, 0}, .divisions = {60, 60}};
     // require number of candidates is >=1 and <=6
     auto intersection_map = wdmcpl::detail::construct_intersection_map(mesh, grid);
     REQUIRE(intersection_map.numRows() == 3600);
     REQUIRE(num_candidates_within_range(intersection_map, 1, 6));
   }
}
TEST_CASE("uniform grid search") {
  using wdmcpl::GridPointSearch;
  //auto lib = Omega_h::Library{};
  auto* lib = omega_h_library;
  auto world = lib->world();
  auto mesh =
    Omega_h::build_box(world, OMEGA_H_SIMPLEX, 1, 1, 1, 10, 10, 0, false);
  GridPointSearch search{mesh,10,10};
  SECTION("global coordinate within mesh")
  {
    Kokkos::View<wdmcpl::Real*[2]> points("points",2);
    Kokkos::parallel_for(1, KOKKOS_LAMBDA(size_t) {
      points(0,0) = 0;
      points(0,1) = 0;
      points(1,0) = 0.55;
      points(1,1) = 0.54;
        });
    auto result = search(points);
    //auto host_result = Kokkos::create_mirror_view_and_copy(Kokkos::DefaultExecutionSpace{}, result);
    //auto host_result = Kokkos::create_mirror_view_and_copy(Kokkos::HostSpace{}, result);
    auto host_result = Kokkos::create_mirror_view(result);
    Kokkos::deep_copy(host_result,result);
    REQUIRE(host_result(0).id == 0);
    REQUIRE(host_result(0).xi[0] == Approx(1));
    REQUIRE(host_result(0).xi[1] == Approx(0));
    REQUIRE(host_result(0).xi[2] == Approx(0));
    REQUIRE(host_result(1).id == 91);
    REQUIRE(host_result(1).xi[0] == Approx(0.5));
    REQUIRE(host_result(1).xi[1] == Approx(0.1));
    REQUIRE(host_result(1).xi[2] == Approx(0.4));

  }
  SECTION("Global coordinate outisde mesh", "[!mayfail]") {
    Kokkos::View<wdmcpl::Real*[2]> points("points",4);
    points(0,0) = 100;
    points(0,1) = 100;
    points(1,0) = 1;
    points(1,1) = 1;
    points(2,0) = -1;
    points(2,1) = -1;
    points(3,0) = 0;
    points(3,1) = 0;
    auto result = search(points);
    auto host_result = Kokkos::create_mirror_view_and_copy(Kokkos::DefaultExecutionSpace{}, result);

    // point outside top right
    REQUIRE(-1*host_result(0).id == host_result(1).id);
    // point outside bottom left
    REQUIRE(-1*host_result(2).id == host_result(3).id);
  }
}
