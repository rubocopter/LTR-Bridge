#include "../src/d3d9_real_bridge_probe/completion.h"

using ltr::d3d9_real_bridge::CompletionResult;
using ltr::d3d9_real_bridge::classify_completion;

int main() {
  if (classify_completion(false, 0, 12, 12, true) != CompletionResult::pass)
    return 1;
  if (classify_completion(false, 90, 2, 12, false) != CompletionResult::fail)
    return 2;
  if (classify_completion(true, 90, 2, 12, false) !=
      CompletionResult::cancelled)
    return 3;
  if (classify_completion(true, 1, 2, 12, false) != CompletionResult::fail)
    return 4;
  return 0;
}
