#include "config/config.h"
#include "mouse/AimbotTarget.h"
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>

Config config{};
std::mutex configMutex;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    try
    {
        using R = ModelClassRole;
        config.class_player = 0; // Deliberately wrong legacy values; explicit roles take precedence.
        config.class_head = 1;
        config.body_y_offset = 0.3f;
        config.head_y_offset = 0.1f;
        config.disable_headshot = false;
        config.model_class_roles = {R::FriendlyBody, R::FriendlyHead, R::Ignore,
            R::EnemyBody, R::EnemyHead, R::EnemyBody, R::EnemyHead};
        const cv::Rect body(280, 270, 80, 120);
        const cv::Rect nearbyFriend(310, 310, 20, 20);
        const auto policy = config.targetClassPolicy();
        check(!policy.eligible(-1, false) && !policy.eligible(99, false), "Invalid IDs must be ignored");
        for (int enemy : {3, 4, 5, 6})
        {
            std::unique_ptr<AimbotTarget> target(sortTargets({nearbyFriend, body}, {0, enemy}, 640, 640, false));
            check(target && target->classId == enemy, "Every assigned enemy class must be targetable; closer friends excluded");
            MultiTargetTracker tracker;
            tracker.update({nearbyFriend, body}, {1, enemy}, 640, 640, false, true);
            LockedTargetInfo locked;
            check(tracker.getLockedTarget(locked) && locked.target.classId == enemy,
                "Tracker must support every enemy body/head ID and exclude friendlies");
        }
        for (int ignored : {0, 1, 2, -1, 99})
        {
            std::unique_ptr<AimbotTarget> target(sortTargets({body}, {ignored}, 640, 640, false));
            check(!target, "Friendly, ignored and invalid classes must never be selected");
            MultiTargetTracker tracker;
            tracker.update({body}, {ignored}, 640, 640, false, true);
            LockedTargetInfo locked;
            check(!tracker.getLockedTarget(locked), "Tracker must not lock friendly/ignored classes");
        }
        config.disable_headshot = true;
        std::unique_ptr<AimbotTarget> target(sortTargets({body, nearbyFriend}, {5, 6}, 640, 640, true));
        check(target && target->classId == 5, "Disabling heads must leave all enemy body classes eligible");
        MultiTargetTracker headDisabled;
        headDisabled.update({body}, {6}, 640, 640, true, true);
        LockedTargetInfo locked;
        check(!headDisabled.getLockedTarget(locked), "Disabled enemy head cannot be tracked");
        config.disable_headshot = false;
        MultiTargetTracker tracker;
        // Head/body fusion must also work for additional classes, not only first IDs.
        tracker.update({body, cv::Rect(300, 270, 25, 20)}, {5, 6}, 640, 640, false, true);
        check(tracker.getDebugTracks().size() == 1 && tracker.getLockedTarget(locked) && locked.target.classId == 5,
            "Additional enemy head/body classes must fuse into one target");
        check(locked.target.pivotY < 280, "Fused body must use the enemy head pivot");
        config.model_class_roles[5] = R::FriendlyBody;
        config.model_class_roles[6] = R::FriendlyHead;
        ++config.model_class_revision;
        check(!tracker.getLockedTarget(locked), "Reclassifying a locked target must invalidate the lock before a new frame");
        tracker.update({}, {}, 640, 640, false, true);
        check(!tracker.getLockedTarget(locked) && tracker.getDebugTracks().empty(),
            "No missed-frame prediction may preserve a target reclassified as friendly");
        tracker.update({body}, {3}, 640, 640, false, true);
        check(tracker.getLockedTarget(locked), "Remaining enemy class must still work");
        ++config.model_class_revision; // Switching models with identical class roles still invalidates old tracks.
        check(!tracker.getLockedTarget(locked), "Model reload must invalidate previous identities");
        tracker.update({}, {}, 640, 640, false, true);
        check(tracker.getDebugTracks().empty(), "Model revision must reset stale tracking");
        // Key routing: holds, releases, conflict, auto-aim and legacy device fallback.
        check(resolveAimTeam(false, false, false) == AimTeam::None, "Released keys must stop aiming");
        check(resolveAimTeam(true, false, false) == AimTeam::Enemy, "Enemy hold routing failed");
        check(resolveAimTeam(false, true, false) == AimTeam::Friendly, "Friendly hold routing failed");
        check(resolveAimTeam(true, true, true, true) == AimTeam::None, "Conflicting holds must stop even with auto/device aim");
        check(resolveAimTeam(false, false, true) == AimTeam::Enemy, "Auto aim must retain enemy default");
        check(resolveAimTeam(false, true, true, true) == AimTeam::Friendly, "Friendly hold must override auto/device fallback");
        check(resolveAimTeam(false, false, false, true) == AimTeam::Enemy, "Legacy device aiming fallback broken");
        config.model_class_roles = {R::FriendlyBody, R::FriendlyHead, R::Ignore,
            R::EnemyBody, R::EnemyHead, R::FriendlyBody, R::FriendlyHead};
        config.setAimTeam(AimTeam::Friendly);
        for (int friendly : {0, 1, 5, 6})
        {
            std::unique_ptr<AimbotTarget> ally(sortTargets({nearbyFriend, body}, {3, friendly}, 640, 640, false));
            check(ally && ally->classId == friendly, "Friendly key must exclude nearer enemies and support all friendly IDs");
            MultiTargetTracker allyTracker;
            allyTracker.update({nearbyFriend, body}, {4, friendly}, 640, 640, false, true);
            check(allyTracker.getLockedTarget(locked) && locked.target.classId == friendly,
                "Friendly tracker selected the wrong team");
        }
        tracker.reset();
        tracker.update({body, cv::Rect(300, 270, 25, 20)}, {5, 6}, 640, 640, false, true);
        check(tracker.getDebugTracks().size() == 1 && tracker.getLockedTarget(locked) && locked.target.pivotY < 280,
            "Friendly head/body fusion must use the friendly head pivot");
        config.disable_headshot = true;
        tracker.update({body}, {6}, 640, 640, true, true);
        check(!tracker.getLockedTarget(locked), "Disabled friendly heads must not target");
        config.disable_headshot = false;
        tracker.update({body}, {5}, 640, 640, false, true);
        check(tracker.getLockedTarget(locked), "Friendly body lock missing");
        config.setAimTeam(AimTeam::Enemy);
        check(!tracker.getLockedTarget(locked), "Changing team must invalidate old lock before a fresh frame");
        tracker.update({}, {}, 640, 640, false, true);
        check(tracker.getDebugTracks().empty(), "Changing team must discard predicted tracks");
        tracker.update({body}, {3}, 640, 640, false, true);
        check(tracker.getLockedTarget(locked), "Enemy targeting must resume after friendly release with auto aim");
        config.setAimTeam(AimTeam::None);
        check(!tracker.getLockedTarget(locked), "Releasing aim must invalidate cached lock");
        std::unique_ptr<AimbotTarget> released(sortTargets({body, nearbyFriend}, {3, 0}, 640, 640, false));
        check(!released, "Inactive team must never select either side");
        tracker.update({body, nearbyFriend}, {3, 0}, 640, 640, false, true);
        check(tracker.getDebugTracks().empty(), "Inactive team must not create tracks");
        config.setAimTeam(AimTeam::Enemy);
        const auto beforeRoundTrip = config.targetClassPolicy();
        const auto editorRevision = config.model_class_revision;
        config.setAimTeam(AimTeam::Friendly);
        config.setAimTeam(AimTeam::Enemy);
        check(!(beforeRoundTrip == config.targetClassPolicy()),
            "Enemy-friendly-enemy transitions must invalidate the original snapshot");
        check(config.model_class_revision == editorRevision, "Aim holds must not reset unsaved role-editor drafts");
        const auto steadyPolicy = config.targetClassPolicy();
        config.setAimTeam(AimTeam::Enemy);
        check(steadyPolicy == config.targetClassPolicy(), "Holding the same key must not reset tracking every poll");
        tracker.update({body}, {3}, 640, 640, false, true);
        check(tracker.getLockedTarget(locked), "Enemy lock required for round-trip regression");
        config.setAimTeam(AimTeam::None);
        config.setAimTeam(AimTeam::Enemy);
        check(!tracker.getLockedTarget(locked), "Release/repress between frames must invalidate stale tracking");
        config.model_class_roles.clear();
        config.class_player = 3; config.class_head = 4;
        check(config.targetClassPolicy().body(3) && config.targetClassPolicy().head(4),
            "Legacy backend configuration must remain usable");
        std::cout << "Target role selection, fusion and stale-lock tests passed\n";
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
