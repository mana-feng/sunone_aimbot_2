#include "config/config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    auto path = std::filesystem::temp_directory_path() / ("sunone-hotkeys-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    try
    {
        { std::ofstream out(path); out << "button_targeting=CapsLock\nbutton_targeting_friendly=LeftAlt,X1MouseButton\n"; }
        Config cfg{};
        check(cfg.loadConfig(path.string()), "Config load failed");
        check(cfg.saveConfig(path.string()), "Config save failed");
        std::ifstream in(path);
        std::string saved((std::istreambuf_iterator<char>(in)), {});
        in.close();
        check(saved.find("button_targeting_friendly = LeftAlt,X1MouseButton") != std::string::npos,
            "Friendly aiming hotkeys must survive load/save");
        check(saved.find("button_targeting = CapsLock") != std::string::npos,
            "Existing enemy hotkey must remain unchanged");
        { std::ofstream out(path); out << "button_targeting=CapsLock\n"; }
        check(cfg.loadConfig(path.string()) && cfg.saveConfig(path.string()), "Legacy config failed");
        std::ifstream legacy(path);
        saved.assign(std::istreambuf_iterator<char>(legacy), {});
        legacy.close();
        check(saved.find("button_targeting_friendly = None") != std::string::npos,
            "Legacy configs must default friendly aiming to unbound");
        std::filesystem::remove(path);
        std::cout << "Aim hotkey persistence and migration tests passed\n";
    }
    catch (const std::exception& e)
    {
        std::filesystem::remove(path);
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
