#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Class IDs are model-local. Labels never determine team or anatomy.
enum class ModelClassRole { Ignore, EnemyBody, EnemyHead, FriendlyBody, FriendlyHead };

inline const char* modelClassRoleToken(ModelClassRole role)
{
    switch (role)
    {
    case ModelClassRole::Ignore: return "ignore";
    case ModelClassRole::EnemyBody: return "enemy_body";
    case ModelClassRole::EnemyHead: return "enemy_head";
    case ModelClassRole::FriendlyBody: return "friendly_body";
    case ModelClassRole::FriendlyHead: return "friendly_head";
    default: throw std::runtime_error("Invalid model class role");
    }
}

inline ModelClassRole parseModelClassRole(const std::string& token)
{
    for (int i = 0; i < 5; ++i)
        if (token == modelClassRoleToken(static_cast<ModelClassRole>(i))) return static_cast<ModelClassRole>(i);
    throw std::runtime_error("Invalid model class role: " + token);
}

inline void validateModelClassRoles(const std::vector<ModelClassRole>& roles, int classes)
{
    if (classes < 1 || classes > 10000 || roles.size() != static_cast<size_t>(classes))
        throw std::runtime_error("Class roles must match this model's class count");
    for (auto role : roles) modelClassRoleToken(role);
}

inline std::vector<ModelClassRole> legacyModelClassRoles(int classes, int body, int head)
{
    std::vector<ModelClassRole> roles(classes, ModelClassRole::Ignore);
    if (body >= 0 && body < classes) roles[body] = ModelClassRole::EnemyBody;
    if (head >= 0 && head < classes && head != body) roles[head] = ModelClassRole::EnemyHead;
    return roles;
}

inline int firstModelClassWithRole(const std::vector<ModelClassRole>& roles, ModelClassRole role)
{
    for (size_t i = 0; i < roles.size(); ++i) if (roles[i] == role) return static_cast<int>(i);
    return -1;
}

enum class AimTeam { None, Enemy, Friendly };

// Explicit holds override auto-aim and legacy device aiming. Conflicting holds stop aiming.
inline AimTeam resolveAimTeam(bool enemyHeld, bool friendlyHeld, bool autoAim, bool legacyDeviceAim = false)
{
    if (enemyHeld && friendlyHeld) return AimTeam::None;
    if (friendlyHeld) return AimTeam::Friendly;
    return enemyHeld || autoAim || legacyDeviceAim ? AimTeam::Enemy : AimTeam::None;
}

struct TargetClassPolicy
{
    std::vector<ModelClassRole> roles;
    int legacyBody = -1, legacyHead = -1;
    uint64_t revision = 0;
    AimTeam team = AimTeam::Enemy;
    uint64_t teamRevision = 0;
    ModelClassRole role(int id) const
    {
        if (id < 0) return ModelClassRole::Ignore;
        if (!roles.empty()) return id < static_cast<int>(roles.size()) ? roles[id] : ModelClassRole::Ignore;
        if (id == legacyBody) return ModelClassRole::EnemyBody;
        if (id == legacyHead) return ModelClassRole::EnemyHead;
        return ModelClassRole::Ignore;
    }
    bool body(int id) const
    {
        return team == AimTeam::Enemy ? role(id) == ModelClassRole::EnemyBody :
            team == AimTeam::Friendly && role(id) == ModelClassRole::FriendlyBody;
    }
    bool head(int id) const
    {
        return team == AimTeam::Enemy ? role(id) == ModelClassRole::EnemyHead :
            team == AimTeam::Friendly && role(id) == ModelClassRole::FriendlyHead;
    }
    bool eligible(int id, bool disableHeadshot) const { return body(id) || (!disableHeadshot && head(id)); }
    bool operator==(const TargetClassPolicy& other) const
    {
        return roles == other.roles && legacyBody == other.legacyBody && legacyHead == other.legacyHead && revision == other.revision && team == other.team && teamRevision == other.teamRevision;
    }
};
