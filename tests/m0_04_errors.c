#include <sunit.h>
#include <msh_parse.h>

#include <string.h>

sunit_ret_t
nocmd(void)
{
	struct msh_sequence *s;
	msh_err_t ret;

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);

	ret = msh_sequence_parse("hello |", s);
	SUNIT_ASSERT("Pipe with missing command after |", ret = MSH_ERR_PIPE_MISSING_CMD);
	msh_sequence_free(s);
	

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);
	ret = msh_sequence_parse("| hello", s);
	SUNIT_ASSERT("Pipe with missing command before |", ret = MSH_ERR_PIPE_MISSING_CMD);

	msh_sequence_free(s);

	return 0;
}

sunit_ret_t
too_many_cmd(void)
{
	struct msh_sequence *s;
	msh_err_t ret;

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);
	ret = msh_sequence_parse("cmd1 | cmd2 | cmd3 | cmd4 | cmd5 | cmd6 | cmd7 | cmd8 | cmd9 | cmd10 | cmd11 | cmd12 | cmd13 | cmd14 | cmd15 | cmd16 | cmd17", s);
	SUNIT_ASSERT("too many commands", ret == MSH_ERR_TOO_MANY_CMDS); // Expect error code -7

	msh_sequence_free(s);

	return 0;
}

sunit_ret_t
too_many_args(void)
{
	struct msh_sequence *s;
	msh_err_t ret;

	s = msh_sequence_alloc();
	SUNIT_ASSERT("sequence allocation", s != NULL);
	ret = msh_sequence_parse("cmd1 cmd2 cmd3 cmd4 cmd5 cmd6 cmd7 cmd8 cmd9 cmd10 cmd11 cmd12 cmd13 cmd14 cmd15 cmd16 cmd17", s);
	SUNIT_ASSERT("too many commands", ret == MSH_ERR_TOO_MANY_ARGS);

	msh_sequence_free(s);

	return 0;
}

int
main(void)
{
	struct sunit_test tests[] = {
		SUNIT_TEST("pipeline with no command after |", nocmd),
		SUNIT_TEST("pipeline with no command before |", nocmd),
		SUNIT_TEST("too many commands", too_many_cmd),
		SUNIT_TEST("too many args", too_many_args),
		/* add your own tests here... */
		SUNIT_TEST_TERM
	};

	sunit_execute("Testing edge cases and errors", tests);

	return 0;
}
