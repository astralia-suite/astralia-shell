#include <cstdio>
#include <iterator>

void test_config();
void test_config_watch();
void test_monitor_overrides();
void test_poll_source();
void test_keyboard();
void test_active_output();
void test_dock();
void test_input_convert();
void test_animated_image();
void test_marquee_scroll();
void test_palette();
void test_image_decode();
void test_text_elide();
void test_bar_fillet();
void test_visualizer_fft();

int main() {
    struct Case {
        const char *name;
        void (*fn)();
    };
    Case cases[] = {
        {"config", test_config},
        {"config_watch", test_config_watch},
        {"monitor_overrides", test_monitor_overrides},

        {"poll_source", test_poll_source},

        {"keyboard", test_keyboard},
        {"active_output", test_active_output},
        {"dock", test_dock},
        {"input_convert", test_input_convert},

        {"animated_image", test_animated_image},
        {"marquee_scroll", test_marquee_scroll},
        {"palette", test_palette},
        {"image_decode", test_image_decode},
        {"text_elide", test_text_elide},
        {"bar_fillet", test_bar_fillet},
        {"visualizer_fft", test_visualizer_fft},
    };
    for (auto &c : cases) {
        std::printf("[ RUN ] %s\n", c.name);
        c.fn();
        std::printf("[ OK  ] %s\n", c.name);
    }
    std::printf("All %zu tests passed.\n", std::size(cases));
}
