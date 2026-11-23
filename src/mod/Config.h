#pragma once

#include <string>
#include <vector>

namespace animated_mojang {

struct Config {
    bool enabled = true;
    bool minecraftTitleAnimation = true;
    bool caveBackground = true;
    bool replayOnReturnToMenu = false;
    bool blockMenuInputDuringIntro = true;
    double maxPresentationSeconds = 15.0;
    std::vector<std::string> menuScreenNames = {
        "start_screen",
        "/main-menu",
        "main_menu_screen"
    };
};

}
