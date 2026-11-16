#include <common.h>
#include "menu_model.c"

static unsigned int failures;

static void expect(bool condition, const char *name)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", name);
	if (!condition)
		failures++;
}

static void parse(struct menu_config *config, const char *text)
{
	menu_config_defaults(config, MENU_DEFAULT_TIMEOUT);
	menu_config_parse(config, text, strlen(text));
}

static bool entries_are(const struct menu_model *model, const char *expected)
{
	char names[128] = "";
	unsigned int index;

	for (index = 0; index < model->count; index++) {
		if (index)
			strcat(names, ",");
		strcat(names, menu_action_name(model->entries[index]));
	}
	return !strcmp(names, expected);
}

static void check_defaults(void)
{
	struct menu_config config;
	struct menu_model model;

	parse(&config, "");
	expect(config.timeout == 10 && config.default_action == MENU_WINDOWS_CE && !config.console &&
	       !config.linux_enabled && !strcmp(config.linux_partition, "1:1"), "config: defaults");
	menu_model_build(&model, &config, true, false, config.default_action);
	expect(entries_are(&model, "windows-ce,windows-ce-stock") && model.selected == 0,
	       "model: Windows CE entries only without a config");
}

static void check_parse(void)
{
	struct menu_config config;

	parse(&config, "\xef\xbb\xbf# comment\r\n  default = linux \r\ntimeout=3\nconsole=yes\nlinux=yes\n"
		       "linux-partition=1:2\nlinux-kernel=boot/zImage\nlinux-dtb=boot/imx28-pwsh2.dtb\n"
		       "linux-initrd=boot/initrd.img\nlinux-cmdline=console=ttyAMA0,115200 root=/dev/mmcblk1p2 rw\n");
	expect(config.default_action == MENU_LINUX && config.timeout == 3 && config.console && config.linux_enabled &&
	       !config.ignored_lines, "config: keys, spaces, CRLF, BOM and comments");
	expect(!strcmp(config.linux_partition, "1:2") && !strcmp(config.linux_kernel, "boot/zImage") &&
	       !strcmp(config.linux_dtb, "boot/imx28-pwsh2.dtb") && !strcmp(config.linux_initrd, "boot/initrd.img"),
	       "config: Linux paths");
	expect(!strcmp(config.linux_cmdline, "console=ttyAMA0,115200 root=/dev/mmcblk1p2 rw"),
	       "config: cmdline keeps everything after the first =");

	parse(&config, "timeout=-1\ntimeout=abc\ntimeout=99999\ndefault=dos\nconsole=maybe\nlinux-partition=1\n"
		       "linux-partition=a:1\nlinux-kernel=bad path\nlinux-kernel=x;reset\nnot a setting\nunknown=1\n=1\n");
	expect(config.ignored_lines == 12 && config.timeout == 10 && config.default_action == MENU_WINDOWS_CE &&
	       !config.console && !strcmp(config.linux_partition, "1:1") && !config.linux_kernel[0],
	       "config: bad values ignored and counted");

	parse(&config, "timeout=0\ndefault=windows-ce-stock\n");
	expect(config.timeout == 0 && config.default_action == MENU_WINDOWS_CE_STOCK, "config: zero timeout");
}

static void check_model(void)
{
	struct menu_config config;
	struct menu_model model;

	parse(&config, "linux=yes\nconsole=yes\ndefault=console\n");
	menu_model_build(&model, &config, true, false, config.default_action);
	expect(entries_are(&model, "windows-ce,windows-ce-stock,linux,console") && model.selected == 3,
	       "model: all entries, default selected");
	menu_model_build(&model, &config, false, false, config.default_action);
	expect(entries_are(&model, "windows-ce,windows-ce-stock,console"), "model: Linux hidden when missing");

	parse(&config, "linux=yes\ndefault=linux\n");
	menu_model_build(&model, &config, false, false, config.default_action);
	expect(entries_are(&model, "windows-ce,windows-ce-stock") && menu_model_selected(&model) == MENU_WINDOWS_CE,
	       "model: hidden default falls back to Windows CE");
	menu_model_build(&model, &config, true, true, MENU_LINUX);
	expect(entries_are(&model, "windows-ce,windows-ce-stock,linux,console") &&
	       menu_model_selected(&model) == MENU_LINUX, "model: console revealed, selection kept");

	menu_model_move(&model, 1);
	expect(menu_model_selected(&model) == MENU_CONSOLE, "model: next");
	menu_model_move(&model, 1);
	expect(menu_model_selected(&model) == MENU_WINDOWS_CE, "model: next wraps");
	menu_model_move(&model, -1);
	expect(menu_model_selected(&model) == MENU_CONSOLE, "model: previous wraps");
}

static void check_linux_commands(void)
{
	struct menu_config config;
	char command[MENU_COMMAND_LENGTH];

	parse(&config, "linux=yes\n");
	expect(menu_linux_target(&config) == MENU_LINUX_BRAINUX && !menu_linux_probe_path(&config) &&
	       !menu_linux_command(&config, command, sizeof(command)) && !strcmp(command, "run linux_boot"),
	       "linux: Brainux without a target");

	parse(&config, "linux=yes\nlinux-kernel=zImage\n");
	expect(!menu_linux_command(&config, command, sizeof(command)) &&
	       !strcmp(command, "load mmc 1:1 0x42000000 zImage; bootz 0x42000000 -"), "linux: kernel only");

	parse(&config, "linux=yes\nlinux-partition=1:2\nlinux-kernel=boot/zImage\nlinux-dtb=boot/a.dtb\n"
		       "linux-initrd=boot/initrd\n");
	expect(!strcmp(menu_linux_probe_path(&config), "boot/zImage") &&
	       !menu_linux_command(&config, command, sizeof(command)) &&
	       !strcmp(command, "load mmc 1:2 0x42000000 boot/zImage; load mmc 1:2 0x41000000 boot/a.dtb; "
				"load mmc 1:2 0x43000000 boot/initrd; bootz 0x42000000 0x43000000:${filesize} 0x41000000"),
	       "linux: kernel, DTB and initrd on a partition");

	parse(&config, "linux=yes\nlinux-dtb=a.dtb\n");
	expect(!menu_linux_command(&config, command, sizeof(command)) && !strcmp(command, "run linux_boot"),
	       "linux: DTB without a kernel keeps Brainux");

	parse(&config, "linux=yes\nlinux-partition=0:4\nlinux-kernel=zImage\nlinux-script=boot.scr\n");
	expect(menu_linux_target(&config) == MENU_LINUX_SCRIPT && !strcmp(menu_linux_probe_path(&config), "boot.scr") &&
	       !menu_linux_command(&config, command, sizeof(command)) &&
	       !strcmp(command, "load mmc 0:4 0x42000000 boot.scr; source 0x42000000"), "linux: script wins over kernel");

	expect(menu_linux_command(&config, command, 16) == -ENOSPC, "linux: short buffer refused");
}

int main(void)
{
	check_defaults();
	check_parse();
	check_model();
	check_linux_commands();
	return failures ? 1 : 0;
}
