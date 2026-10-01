#include "../src/d3d9_real_bridge_probe/protocol.h"

#include <cstdint>
#include <limits>

int main() {
  using ltr::d3d9_real_bridge::fence_reached;
  using ltr::d3d9_real_bridge::pressure_requirement_satisfied;

  if (!fence_reached(7, 7))
    return 1;
  if (!fence_reached(8, 7))
    return 2;
  if (fence_reached(6, 7))
    return 3;
  if (fence_reached(std::numeric_limits<std::uint64_t>::max(), 7))
    return 4;
  if (!pressure_requirement_satisfied(2, 50, 0))
    return 5;
  if (pressure_requirement_satisfied(3, 50, 0))
    return 6;
  if (!pressure_requirement_satisfied(3, 50, 1))
    return 7;
  if (!pressure_requirement_satisfied(12, 0, 0))
    return 8;
  return 0;
}
