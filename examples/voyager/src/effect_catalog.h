// The effects a moment may do, and the code that applies each.
//
// THE EFFECT CONTRACT. The book closes the list ("It may move a
// characteristic ... It may bind a standing ... It may leave a mark
// ... It may turn the life") and the graph holds each kind, cited.
// This file binds each key to the code that validates and applies
// it. Like the primitive catalog, a name is spelled here ONCE, as the
// seam the graph routes to; the book test excludes catalogs from its
// literal scan for exactly that reason, and nowhere else may a key be
// spelled.
//
// Handlers ADD OPS, they never write. All the effects of one
// resolution land together or none of them do, which is the book's
// first combination rule, and the batch is what makes it true.
//
// Every number a handler enforces is read from the graph at the point
// of use: the characteristic move limit is a RuleConstant, the
// standings and turns are the entities the extractor seeded.

#ifndef VOYAGER_EFFECT_CATALOG_H
#define VOYAGER_EFFECT_CATALOG_H

#include "graph_ops.h"

#include "logosphere/kg/kg_module.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace voyager {

// One effect as the director wrote it: `key | arg | arg ...`.
struct Effect {
    std::string key;
    std::vector<std::string> args;
    std::string text;   // the line as written, for records and refusals
};

// What a handler applies into: the world, the character, the context
// that captured world entities belong to, and the batch to extend.
struct EffectSite {
    kg::KGModule& world;
    kg::EntityID character;
    kg::EntityID context;
    std::vector<kg::KGOp>& ops;
    int& next_alias;
    // The alias of the moment being resolved in this batch, so what an
    // effect leaves points back at the moment that left it.
    std::string moment_alias;
};

struct EffectHandler {
    // The argument shape, for the director's brief: "| <short name> |
    // <signed amount>". The catalog owns it, so the brief is built
    // from the registry and the session spells no effect.
    std::string usage;
    // One worked example line, built from the graph's own words, so a
    // small model sees the shape filled in rather than described.
    std::function<std::string(const kg::KGModule&)> example;
    std::function<bool(const kg::KGModule&, kg::EntityID, const Effect&,
                       std::string&)> validate;
    std::function<bool(EffectSite&, const Effect&, std::string&)> apply;
};

// The first key of a vocabulary type, for examples.
inline std::string first_key(const kg::KGModule& world,
                             const std::string& type,
                             const std::string& slot) {
    for (const kg::EntityID id : world.findByType(type)) {
        return world.getProperty(id, slot);
    }
    return {};
}

inline std::string fresh_alias(int& counter, const std::string& stem) {
    return stem + std::to_string(++counter);
}

inline bool constant_int(const kg::KGModule& world, const std::string& name,
                         long long& value, std::string& error) {
    for (const kg::EntityID id : world.findByType("RuleConstant")) {
        if (world.getProperty(id, "name") != name) continue;
        if (as_int(world.getProperty(id, "constant_value"), value)) {
            return true;
        }
        error = "the rule constant '" + name + "' holds no readable number";
        return false;
    }
    error = "the rules fix no constant named '" + name + "'";
    return false;
}

// The slot a short name on the sheet stands for, read off the
// Characteristic entities. The director writes the short name as the
// sheet prints it, the graph says which slot that is; this code names
// neither.
inline kg::EntityID characteristic_by_label(const kg::KGModule& world,
                                            const std::string& label) {
    for (const kg::EntityID id : world.findByType("Characteristic")) {
        if (world.getProperty(id, "characteristic_abbreviation") == label) {
            return id;
        }
    }
    return kg::INVALID_ENTITY;
}

// An entity of a world type by name inside one context, or nothing.
inline kg::EntityID captured_by_name(const kg::KGModule& world,
                                     const std::string& type,
                                     const std::string& name,
                                     kg::EntityID context) {
    for (const kg::EntityID id : world.findByType(type)) {
        if (world.getProperty(id, "name") != name) continue;
        if (world.getProperty(id, "origin_context") ==
            std::to_string(context)) {
            return id;
        }
    }
    return kg::INVALID_ENTITY;
}

inline std::map<std::string, EffectHandler> make_effect_registry() {
    std::map<std::string, EffectHandler> registry;

    // move_characteristic | <short name> | <signed delta>
    registry["move_characteristic"] = {
        "| <short name on the sheet> | <+1 or -1>",
        [](const kg::KGModule& world) {
            return "move_characteristic | " +
                   first_key(world, "Characteristic",
                             "characteristic_abbreviation") +
                   " | -1";
        },
        [](const kg::KGModule& world, kg::EntityID character,
           const Effect& effect, std::string& error) {
            if (effect.args.size() != 2) {
                error = "'" + effect.text +
                        "': a move names a characteristic and a signed "
                        "amount, nothing else";
                return false;
            }
            const kg::EntityID characteristic =
                characteristic_by_label(world, effect.args[0]);
            if (characteristic == kg::INVALID_ENTITY) {
                error = "'" + effect.text + "': no characteristic on the "
                        "sheet is called '" + effect.args[0] + "'";
                return false;
            }
            long long delta = 0;
            long long limit = 0;
            if (!as_int(effect.args[1], delta) || delta == 0) {
                error = "'" + effect.text + "': the amount is not a signed "
                        "whole number";
                return false;
            }
            if (!constant_int(world, "characteristic_move_limit", limit,
                              error)) {
                return false;
            }
            if (std::llabs(delta) > limit) {
                error = "'" + effect.text + "': the book moves a "
                        "characteristic by " + std::to_string(limit) +
                        " and never more in one moment";
                return false;
            }
            // The slot's own range, declared in the schema, refused
            // here rather than at the write so the director is told
            // and can price the door again.
            const std::string slot =
                world.getProperty(characteristic, "attribute_ref");
            long long current = 0;
            if (!as_int(world.getProperty(character, slot), current)) {
                error = "'" + effect.text + "': the score is unreadable";
                return false;
            }
            const auto* def =
                world.getRegistry().findProperty("Character", slot);
            const double after = static_cast<double>(current + delta);
            if (def && def->has_min && after < def->min_value) {
                error = "'" + effect.text + "': " + effect.args[0] +
                        " is " + std::to_string(current) +
                        " and cannot fall below " +
                        std::to_string(static_cast<long long>(def->min_value));
                return false;
            }
            if (def && def->has_max && after > def->max_value) {
                error = "'" + effect.text + "': " + effect.args[0] +
                        " is " + std::to_string(current) +
                        " and cannot rise above " +
                        std::to_string(static_cast<long long>(def->max_value));
                return false;
            }
            return true;
        },
        [](EffectSite& site, const Effect& effect, std::string& error) {
            const kg::EntityID characteristic =
                characteristic_by_label(site.world, effect.args[0]);
            const std::string slot =
                site.world.getProperty(characteristic, "attribute_ref");
            long long current = 0;
            long long delta = 0;
            if (!as_int(site.world.getProperty(site.character, slot),
                        current) ||
                !as_int(effect.args[1], delta)) {
                error = "'" + effect.text + "': the score is unreadable";
                return false;
            }
            site.ops.push_back(set_property(site.character, slot,
                                            std::to_string(current + delta)));
            return true;
        }};

    // bind_standing | <standing key> | <Person|Place|Faction> | <name>
    registry["bind_standing"] = {
        "| <standing> | Person or Place or Faction | <their name>",
        [](const kg::KGModule& world) {
            return "bind_standing | " +
                   first_key(world, "StandingKind", "standing_key") +
                   " | Person | the harbourmaster at Kell's Landing";
        },
        [](const kg::KGModule& world, kg::EntityID, const Effect& effect,
           std::string& error) {
            if (effect.args.size() != 3 || effect.args[2].empty()) {
                error = "'" + effect.text + "': a standing names what it "
                        "is, what kind of thing holds it, and who";
                return false;
            }
            bool known = false;
            for (const kg::EntityID id : world.findByType("StandingKind")) {
                if (world.getProperty(id, "standing_key") == effect.args[0]) {
                    known = true;
                }
            }
            if (!known) {
                error = "'" + effect.text + "': '" + effect.args[0] +
                        "' is not a standing the book names";
                return false;
            }
            const std::string& type = effect.args[1];
            if (type != "Person" && type != "Place" && type != "Faction") {
                error = "'" + effect.text + "': a standing is held toward "
                        "a Person, a Place or a Faction";
                return false;
            }
            return true;
        },
        [](EffectSite& site, const Effect& effect, std::string& error) {
            (void)error;
            const std::string& type = effect.args[1];
            const std::string& name = effect.args[2];
            kg::EntityID existing =
                captured_by_name(site.world, type, name, site.context);
            std::string with;
            if (existing == kg::INVALID_ENTITY) {
                // New to the world: created at the scope of this
                // playing, which is what origin_context says.
                const std::string alias = fresh_alias(site.next_alias, "w");
                // Its identity is the playing's context plus a machine
                // key made from its type and name, the same shape the
                // seed loader gives a book's entities.
                std::string key = type + ":";
                for (const char c : name) {
                    key += (std::isalnum(static_cast<unsigned char>(c)))
                               ? static_cast<char>(std::tolower(
                                     static_cast<unsigned char>(c)))
                               : '-';
                }
                site.ops.push_back(create_entity(
                    type, alias,
                    {{"name", name},
                     {"origin_context", std::to_string(site.context)},
                     {"identity_context", std::to_string(site.context)},
                     {"entity_key", key}}));
                with = "@" + alias;
            } else {
                with = std::to_string(existing);
            }
            const std::string held = fresh_alias(site.next_alias, "s");
            site.ops.push_back(create_entity(
                "StandingHeld", held,
                {{"name", effect.args[0] + " with " + name},
                 {"event_type", "STANDING_HELD"},
                 {"standing_key", effect.args[0]},
                 {"standing_with", with},
                 {"left_by", "@" + site.moment_alias}}));
            site.ops.push_back(relate(site.character, "LIVED", held));
            return true;
        }};

    // leave_mark | <the mark, in words>
    registry["leave_mark"] = {
        "| <the mark, in a few words>",
        [](const kg::KGModule&) {
            return std::string("leave_mark | a name the dock crews remember");
        },
        [](const kg::KGModule&, kg::EntityID, const Effect& effect,
           std::string& error) {
            if (effect.args.empty() || effect.args[0].empty()) {
                error = "'" + effect.text + "': a mark has words";
                return false;
            }
            return true;
        },
        [](EffectSite& site, const Effect& effect, std::string& error) {
            (void)error;
            std::string text;
            for (const auto& arg : effect.args) {
                if (!text.empty()) text += " | ";
                text += arg;
            }
            const std::string alias = fresh_alias(site.next_alias, "m");
            site.ops.push_back(create_entity(
                "MarkLeft", alias,
                {{"name", text}, {"event_type", "MARK_LEFT"},
                 {"mark_text", text},
                 {"left_by", "@" + site.moment_alias}}));
            site.ops.push_back(relate(site.character, "LIVED", alias));
            return true;
        }};

    // turn_life | <turn key>
    registry["turn_life"] = {
        "| <one of the turns>",
        [](const kg::KGModule& world) {
            return "turn_life | " + first_key(world, "Turn", "turn_key");
        },
        [](const kg::KGModule& world, kg::EntityID, const Effect& effect,
           std::string& error) {
            if (effect.args.size() != 1) {
                error = "'" + effect.text + "': a turn names one way";
                return false;
            }
            for (const kg::EntityID id : world.findByType("Turn")) {
                if (world.getProperty(id, "turn_key") == effect.args[0]) {
                    return true;
                }
            }
            error = "'" + effect.text + "': '" + effect.args[0] +
                    "' is not a way the book lets a life turn";
            return false;
        },
        [](EffectSite& site, const Effect& effect, std::string& error) {
            (void)error;
            const std::string alias = fresh_alias(site.next_alias, "t");
            site.ops.push_back(create_entity(
                "TurnTaken", alias,
                {{"name", effect.args[0]}, {"event_type", "TURN_TAKEN"},
                 {"turn_key", effect.args[0]},
                 {"left_by", "@" + site.moment_alias}}));
            site.ops.push_back(relate(site.character, "LIVED", alias));
            return true;
        }};

    return registry;
}

}  // namespace voyager

#endif  // VOYAGER_EFFECT_CATALOG_H
