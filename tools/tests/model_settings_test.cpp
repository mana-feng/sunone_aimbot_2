#include "model_metadata.h"
#include "model_profile.h"
#include <iostream>

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

int main()
{
    try
    {
        const auto list = parseModelClassNames("['tbody', 'ctbody']");
        check(list.size() == 2 && list.at(0) == "tbody" && list.at(1) == "ctbody",
            "Python list metadata must expose both class names");
        const auto dict = parseModelClassNames("{1: 'body', 0: 'me'}");
        check(dict.at(0) == "me" && dict.at(1) == "body", "IDs must not depend on dictionary order");
        const auto unicode = parseModelClassNames(R"({"0":"\u654c\u4eba", "1":"head"})");
        check(unicode.at(0) == u8"敌人", "JSON Unicode escapes must be decoded");
        check(parseModelClassNames("['enemy, body', 'it\\'s head']").at(1) == "it's head",
            "Quoted commas and escaped quotes must not create extra classes");
        check(parseModelClassNames("{0: 'body', 0: 'head'}").empty(), "Reject duplicate IDs");
        check(parseModelClassNames("{999999: 'body'}").empty(), "Bound class IDs");
        check(parseModelClassNames("['body'] + run()").empty(), "Do not accept trailing expressions");
        check(parseModelClassNames("malformed").empty(), "Invalid metadata must not invent labels");

        const auto folder = std::filesystem::temp_directory_path() /
            ("sunone-model-settings-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directory(folder);
        const auto model = folder / std::filesystem::path(L"测试.onnx");
        auto sidecar = model; sidecar += L".ini";
        {
            std::ofstream out(sidecar);
            out << "# preserve comment\nlayout=prediction\nclasses=2\nobjectness=1\n"
                << "anchors.output=10,13,16,30,33,23\nclass_player=-1\nclass_head=-1\n"
                << "class_player=-1\nsource_sha256=example\n";
        }
        saveModelTargetClasses(model, 2, 1, -1);
        const auto p = loadModelProfile(model);
        check(p.player == 1 && p.head == -1, "Saved choice must survive reload, including duplicate old keys");
        check(p.layout == "prediction" && p.anchors.at("output")[0] == 10,
            "Saving class choices must preserve decoder settings");
        std::ifstream in(sidecar);
        const std::string before((std::istreambuf_iterator<char>(in)), {});
        in.close();
        check(before.find("# preserve comment") != std::string::npos &&
            before.find("source_sha256=example") != std::string::npos, "Preserve provenance and comments");
        bool rejected = false;
        try { saveModelTargetClasses(model, 2, 2, -1); } catch (...) { rejected = true; }
        check(rejected, "Reject out of range class selection");
        std::ifstream afterStream(sidecar);
        check(std::string((std::istreambuf_iterator<char>(afterStream)), {}) == before,
            "Invalid choices must leave the original file unchanged");
        afterStream.close();
        rejected = false;
        try { saveModelTargetClasses(model, 2, 0, 0); } catch (...) { rejected = true; }
        check(rejected, "Body and head must not use the same class ID");
        const auto other = folder / "other.onnx";
        saveModelTargetClasses(other, 2, 0, 1);
        check(loadModelProfile(other).head == 1 && loadModelProfile(model).player == 1,
            "Models must retain independent selections; absent sidecar can be created");
        { std::ofstream out(other.string() + ".ini", std::ios::app); out << "class_roles=enemy_body,friendly_head\n"; }
        loadModelProfile(other); // Explicit roles must survive a normal model profile load.
        using R = ModelClassRole;
        const auto arbitrary = folder / "arbitrary.onnx";
        {
            std::ofstream out(arbitrary.string() + ".ini");
            out << "names=['alpha##x','unknown','red','blue','another','custom']\n"
                << "layout=feature\nsource_sha256=preserve-me\n"
                << "class_roles=ignore,ignore,ignore,ignore,ignore,ignore\n"
                << "class_roles=ignore,ignore,ignore,ignore,ignore,ignore\n";
        }
        const std::vector<R> roles{R::FriendlyBody, R::EnemyHead, R::Ignore, R::EnemyBody, R::FriendlyHead, R::EnemyBody};
        saveModelClassRoles(arbitrary, 6, roles);
        const auto assigned = loadModelProfile(arbitrary);
        check(assigned.classRoles == roles, "All five roles and multiple enemy bodies must survive save/reload");
        check(assigned.player == 3 && assigned.head == 1, "Legacy fields must never point at friendlies");
        check(assigned.names.find("alpha##x") != std::string::npos && assigned.layout == "feature",
            "Arbitrary raw labels and decoder settings must survive role assignment");
        check(loadModelProfile(other).classRoles == std::vector<R>{R::EnemyBody, R::FriendlyHead},
            "Role mappings must not leak between models");
        for (auto invalid : {std::vector<R>{R::EnemyBody}, std::vector<R>(6, static_cast<R>(99))})
        {
            rejected = false;
            try { saveModelClassRoles(arbitrary, 6, invalid); } catch (...) { rejected = true; }
            check(rejected && loadModelProfile(arbitrary).classRoles == roles, "Invalid role save must preserve all selections");
        }
        for (const auto* invalid : {"enemy_body,friend", "enemy_body,", "enemy_head,,ignore", "enemy_body", ""})
        {
            { std::ofstream out(other.string() + ".ini"); out << "classes=2\nclass_roles=" << invalid << '\n'; }
            rejected = false;
            try { loadModelProfile(other); } catch (...) { rejected = true; }
            check(rejected, "Malformed or mismatched role lists must not silently enable classes");
        }
        std::filesystem::remove(arbitrary.string() + ".ini");
        // An invalid parent must report failure rather than silently applying unsaved values.
        rejected = false;
        try { saveModelTargetClasses(folder / "missing" / "model.onnx", 2, 0, -1); }
        catch (...) { rejected = true; }
        check(rejected, "File write errors must be reported");
        std::filesystem::remove(sidecar);
        auto otherSidecar = other; otherSidecar += L".ini";
        std::filesystem::remove(otherSidecar);
        std::filesystem::remove(folder);
        std::cout << "Model metadata and settings tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
