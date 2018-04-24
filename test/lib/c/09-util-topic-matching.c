#include <stdio.h>
#include <stdlib.h>
#include <mosquitto.h>

#define EXPECT_MATCH(A, B) do_check((A), (B), false)
#define EXPECT_NOMATCH(A, B) do_check((A), (B), true)

void do_check(const char *sub, const char *topic, bool bad_res)
{
	bool match;

	mosquitto_topic_matches_sub(sub, topic, &match);
	
	if(match == bad_res){
		printf("s: %s t: %s\n", sub, topic);
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	EXPECT_MATCH("foo/#", "foo/");
	EXPECT_NOMATCH("foo#", "foo");
	EXPECT_NOMATCH("fo#o/", "foo");
	EXPECT_NOMATCH("foo#", "fooa");
	EXPECT_NOMATCH("foo+", "foo");
	EXPECT_NOMATCH("foo+", "fooa");

	EXPECT_NOMATCH("test/6/#", "test/3");
	EXPECT_MATCH("foo/bar", "foo/bar");
	EXPECT_MATCH("foo/+", "foo/bar");
	EXPECT_MATCH("foo/+/baz", "foo/bar/baz");

	EXPECT_MATCH("A/B/+/#", "A/B/B/C");

	EXPECT_MATCH("foo/+/#", "foo/bar/baz");
	EXPECT_MATCH("foo/+/#", "foo/bar");
	EXPECT_MATCH("#", "foo/bar/baz");
	EXPECT_MATCH("#", "foo/bar/baz");

	EXPECT_NOMATCH("foo/bar", "foo");
	EXPECT_NOMATCH("foo/+", "foo/bar/baz");
	EXPECT_NOMATCH("foo/+/baz", "foo/bar/bar");

	EXPECT_NOMATCH("foo/+/#", "fo2/bar/baz");

	EXPECT_MATCH("#", "/foo/bar");
	EXPECT_MATCH("/#", "/foo/bar");
	EXPECT_NOMATCH("/#", "foo/bar");


	EXPECT_MATCH("foo//bar", "foo//bar");
	EXPECT_MATCH("foo//+", "foo//bar");
	EXPECT_MATCH("foo/+/+/baz", "foo///baz");
	EXPECT_MATCH("foo/bar/+", "foo/bar/");

	EXPECT_MATCH("$SYS/bar", "$SYS/bar");
	EXPECT_NOMATCH("#", "$SYS/bar");
	EXPECT_NOMATCH("$BOB/bar", "$SYS/bar");

	return 0;
}

