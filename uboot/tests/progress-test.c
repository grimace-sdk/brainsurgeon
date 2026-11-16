#include <common.h>
#include "progress.c"

static unsigned int failures;
static unsigned int pushes;
static unsigned int last_filled;
static bool bar_available = true;
static unsigned int bar_interval_ms;

int brain_progress_bar_open(struct brain_progress_bar *opened)
{
	if (!bar_available)
		return -ENODEV;
	opened->left = 10;
	opened->top = 20;
	opened->width = 104;
	opened->height = 8;
	opened->interval_ms = bar_interval_ms;
	return 0;
}

int brain_progress_bar_push(const u16 *pixels, const struct brain_progress_bar *pushed)
{
	unsigned int filled = 0;

	while (filled < pushed->width - 4 && pixels[3 * pushed->width + 2 + filled] == COLOUR_FILLED)
		filled++;
	last_filled = filled;
	pushes++;
	return 0;
}

static void expect(bool condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

static void check_redraws(void)
{
	unsigned int done;

	pushes = 0;
	brain_progress_begin(1000000);
	expect(pushes == 1 && last_filled == 0, "progress: begin draws an empty bar");
	for (done = 0; done <= 1000000; done += 100)
		brain_progress_update(done, 1000000);
	expect(pushes == 101 && last_filled == 100, "progress: redrawn only when the filled width changes");
	brain_progress_end();
	expect(pushes == 101, "progress: end does not redraw a full bar");
	brain_progress_update(0, 1);
	expect(pushes == 101, "progress: no drawing after end");
}

static void check_steps(void)
{
	pushes = 0;
	brain_progress_begin(400);
	brain_progress_step(100);
	brain_progress_update(1, 2);
	expect(last_filled == 12, "progress: first step covers its share");
	brain_progress_update(5, 2);
	expect(last_filled == 25, "progress: done past total is clamped to the step");
	brain_progress_step(300);
	brain_progress_update(1, 3);
	expect(last_filled == 50, "progress: second step starts after the first");
	brain_progress_step(0);
	brain_progress_update(1, 1);
	expect(last_filled == 100, "progress: an empty step keeps the position");
	brain_progress_end();

	brain_progress_begin(10);
	brain_progress_update(1, 10);
	brain_progress_end();
	expect(last_filled == 100, "progress: end fills the bar");
}

static void check_no_bar(void)
{
	bar_available = false;
	pushes = 0;
	brain_progress_begin(10);
	brain_progress_update(5, 10);
	brain_progress_end();
	expect(pushes == 0, "progress: nothing drawn without a bar");
	bar_available = true;
}

static void check_interval(void)
{
	bar_interval_ms = 200;
	pushes = 0;
	brain_progress_begin(10);
	brain_progress_update(5, 10);
	expect(pushes == 1, "progress: no redraw within the bar's interval");
	brain_progress_end();
	expect(pushes == 2 && last_filled == 100, "progress: end draws the full bar despite the interval");
	bar_interval_ms = 0;
}

int main(void)
{
	check_redraws();
	check_steps();
	check_no_bar();
	check_interval();
	return failures ? 1 : 0;
}
