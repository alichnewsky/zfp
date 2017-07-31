#include "src/decode1d.c"

#include "constants/1dDouble.h"
#include "utils/rand64.h"
#include "utils/hash64.h"
#include "zfpDecodeBlockBase.c"

int main()
{
  const struct CMUnitTest tests[] = {
    cmocka_unit_test_setup_teardown(when_seededRandomDataGenerated_expect_ChecksumMatches, setup, teardown),
    cmocka_unit_test_setup_teardown(given_1dDoubleBlock_when_DecodeBlock_expect_ReturnValReflectsNumBitsReadFromBitstream, setup, teardown),
    cmocka_unit_test_setup_teardown(given_1dDoubleBlock_when_DecodeBlock_expect_ArrayChecksumMatches, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
