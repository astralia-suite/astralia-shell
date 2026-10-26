#include "service/brightness_service.h"

#include "check.h"

void check_brightness() {
    using namespace astralia;
    using test::check;

    std::vector<DdcBus> found = parse_ddc_detect("Display 1\n"
                                                 "   I2C bus:  /dev/i2c-5\n"
                                                 "   DRM connector:           card1-DP-1\n"
                                                 "   Monitor:  DEL:DELL U2720Q:ABC\n"
                                                 "\n"
                                                 "Display 2\n"
                                                 "   I2C bus:  /dev/i2c-7\n"
                                                 "   DRM connector:           card1-HDMI-A-1\n"
                                                 "\n"
                                                 "Invalid display\n"
                                                 "   I2C bus:  /dev/i2c-9\n"
                                                 "   DRM connector:           card1-DP-3\n");
    check(found.size() == 2, "only listed displays are DDC buses");
    check(found[0].output == "DP-1" && found[0].bus == "5", "the connector drops the card prefix");
    check(found[1].output == "HDMI-A-1" && found[1].bus == "7",
          "connector names keep their dashes");
    check(parse_ddc_detect("").empty(), "no ddcutil output means no displays");

    std::vector<DdcBus> real = parse_ddc_detect(
        "Display 1\n"
        "   I2C bus:          /dev/i2c-7\n"
        "   DRM connector:    card1-DP-2\n"
        "   drm_connector_id: 837\n"
        "   Monitor:          SAM:LC34G55T:HNTXA00054\n"
        "\n"
        "Display 2\n"
        "   I2C bus:          /dev/i2c-8\n"
        "   DRM connector:    card1-DP-3\n"
        "   drm_connector_id: 841\n"
        "   Monitor:          SAM:Odyssey G91F:HNTY700002\n");
    check(real.size() == 2 && real[0].output == "DP-2" && real[0].bus == "7" &&
              real[1].output == "DP-3" && real[1].bus == "8",
          "ddcutil's real two-monitor output parses");

    auto value = parse_ddc_brightness("VCP 10 C 35 100\n");
    check(value && value->first == 35 && value->second == 100, "brightness is current and max");
    check(!parse_ddc_brightness("VCP 12 C 35 100"), "other VCP codes are ignored");
    check(!parse_ddc_brightness("Display not found"), "an error is not a value");
    check(!parse_ddc_brightness("VCP 10 C 0 0"), "a zero max is not usable");
}
