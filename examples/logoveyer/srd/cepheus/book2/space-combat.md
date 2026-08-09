# Chapter 10: Space Combat

Space combat is a staple in classic science fiction, and the Cepheus Engine has rules to cover it. This chapter details the basics for space combat. These rules for vehicle and starship combat presented here are designed to allow for more roleplaying and involvement of the characters. Movement and maneuvering are abstracted to allow for cinematic battles as vessels attempt to maneuver into a position of pursuit and advantage against their opponents, or frantically try to shake pursuit.

Range is similarly abstracted, needing only to note whether the range for all vessels involved for each round is Close, Short, Medium, Long, Very Long, or Extreme.

## Space Combat Checklist

Like [personal combat](../book1/personal-combat.md), space combat in the Cepheus Engine is cyclical. Everybody acts in turn in a regular cycle called a turn. Each turn in space combat lasts one kilosecond. Generally, space combat runs in the following way:

1.  The Referee determines the range at which the encounter begins
2.  All crew members are assigned to a position on board their vessel
3.  The Referee determines which characters are aware of their opponents at the start of the battle. If some but not all vessels are aware of their opponents, the vessels that are aware of their opponents are considered to get an automatic 12 on their initiative roll, giving them an Initiative of 12 + Dexterity DM
4.  Any remaining vessels roll initiative. All vessels are now ready to begin their first turn of combat
5.  All vessels act in initiative order
    1.  At the start of each combat turn, a Captain may declare that their crew is acting hastily
    2.  The crew members of the vessel resolve their actions
    3.  After every crew member has completed their actions, any damage is resolved if the vessel's weapon systems hit enemy ships
6.  When every vessel has had a turn, the combatant with the highest initiative total acts again, and Step 5 repeats until combat ends

## Range in Space Combat

If two vessels randomly encounter each other while travelling through the depths of space, far from any other objects or vessels, the encounter will begin at Very Long range. More often, ships engage near a planet, where the range is Short or Medium.

| Range | Distance | Sensor Details Detected |
| --- | --- | --- |
| Adjacent | <1 km | Individual sources of neural activity (Very Advanced sensors only) |
| Close | 1 to 10 km | Individual ship systems, level of neural activity (Very Advanced sensors only) |
| Short | 10 to 1250 km | Fine visual details, individual heat sources, internal structure (Advanced and Very Advanced sensors only), presence of neural activity (Very Advanced sensors only) |
| Medium | 1250 to 10,000 km | Source of EM emissions, external structure (Advanced and Very Advanced sensors only) |
| Long | 10,000 km to 25,000 km | Ship configuration and shape, thermal activity, external structure (Advanced and Very Advanced sensors only) |
| Very Long | 25,000 km to 50,000 km | Ship's presence and level of activity, basic object silhouette |
| Distant | 50,000 km+ |  |

| Weapon | Adjacent | Close | Short | Medium | Long | Very Long | Distant |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Pulse Laser | Difficult | Difficult | Average | Difficult | Difficult | Very Difficult | — |
| Beam Laser | Difficult | Difficult | Difficult | Average | Difficult | Difficult | Difficult |
| Particle Beam | Very Difficult | Difficult | Difficult | Difficult | Average | Difficult | Difficult |
| Fusion Gun | Difficult | Difficult | Difficult | Average | Difficult | Difficult | Difficult |
| Meson Gun | Very Difficult | Very Difficult | Difficult | Difficult | Average | Difficult | Difficult |
| Sandcaster | Routine | Average | Difficult | — | — | — | — |

## Crew Positions

At the start of an engagement, all crew must be assigned to a position on board ship. There can be only one pilot, but other than that, any number of people can occupy the same position.

| Position | Responsibility |
| --- | --- |
| Bay Gunner | Each bay weapon has its own gunner. |
| Captain | Commands the ship, and can use [Leadership](../book1/skills.md#leadership) and [Tactics](../book1/skills.md#tactics) skills. |
| Chief Security Officer | Commands marines, and can use [Leadership](../book1/skills.md#leadership) and [Tactics](../book1/skills.md#tactics) skills in abstract boarding actions. |
| Damage Control | A character assigned to free-floating damage control can repair any system. |
| Drive Engineer | An engineer can be assigned to each of the M-drive and the J-drive. |
| Marine | Prepares to repel boarders, or to board enemy ships. |
| Passenger | Passengers are all people aboard ship who are not assigned a position and are assumed to be waiting in staterooms. |
| Pilot | Flies the ship, responsible for changing course and for evasive maneuvers. |
| Sensors Operator | A character assigned to communications and sensors. |
| Turret Gunner | Each turret has its own gunner. A character must choose which turret he is manning at the start of the combat. |

### Automated Positions

The ship's computer can cover several positions if it is running the appropriate software:

-   Fire Control programs can either act as **gunners** or aid existing gunners.
-   A ship equipped with repair drones and Auto-Repair software acts as **damage control**.
-   A ship running an Intellect program and Expert Pilot can be the **pilot**.
-   A ship equipped with repair drones and running an Intellect program and Expert Engineer can be a **drive engineer**.

## Initiative

Each ship in an engagement rolls 2D6 to determine their starting Initiative score. The ship with a greater Thrust score gains a +1 DM to its roll.

The Captain of each vessel (or each fleet, if more than one ship is involved on each side) may a Tactics check. The Effect is added to the Initiative of the vessel (or fleet).

## The Space Combat Turn

Each turn in space combat lasts around one kilosecond (1,000 seconds) of game time. In a combat turn, vessels have individual Initiative. Actions are taken in descending order of Initiative. If two vessels have the same Initiative, the vessel with the highest Thrust goes first. If they are still tied, then vessels act simultaneously. When a vessel acts, the crew members of the vessel take all of their actions at once. Each crew member gets a minor action and a significant action.

Once everyone has acted a combat turn is over and a new turn begins. Initiative is not re-rolled but is dynamic, and may be adjusted up and down by actions taken during a turn.

## Note on Personal Actions

As a space combat turn represents over 150 personal combat rounds, it stands to reason that crew members may engage in a lot of personal actions over the course of a single combat turn. Much of this is abstracted into the various space combat actions. Most personal actions have minimal impact on space combat. For crew members that do want to pursue a personal action during space combat, such as use a psionic ability, these actions count as minor actions on the space combat scale. This glosses over the remainder of the character's time during the combat turn, and highlights the primary acts that a character might want to pursue without taking away from the rest of the space combat.

| Description | Type | Crew Member |
| --- | --- | --- |
| [Change Positions](#change-positions) | Minor | Anyone |
| [Personal Action](#personal-action) | Minor | Anyone |
| [Reload Weapons System](#reload-weapons-system) | Significant | Anyone |
| Miscellaneous | Varies | Anyone |
| [Coordinate Crew](#coordinate-crew) | Significant | Captain |
| [Increase Initiative](#increase-initiative) | Significant | Captain |
| [Boarding Action](#boarding-action) | Significant | Chief Security Officer, Marine |
| [Repair Damaged System](#repair-damaged-system) | Significant | Damage Control |
| [Fire Sand](#fire-sand) | Reaction | Gunner |
| [Point Defense](#point-defense) | Reaction | Gunner |
| [Trigger Screens](#trigger-screens) | Reaction | Gunner |
| [Attack](#attack) | Significant | Gunner |
| [Calculate Jump Plot](#calculate-jump-plot) | Significant | Navigator |
| [Range Check](#range-check) | Significant | Navigator |
| [Adjust Speed](#adjust-speed) | Minor | Pilot |
| [Maintain Course](#maintain-course) | Minor | Pilot |
| [Dodge Incoming Fire](#dodge-incoming-fire) | Reaction | Pilot |
| [Avoid Collision](#avoid-collision) | Significant | Pilot |
| [Break Pursuit](#break-pursuit) | Significant | Pilot |
| [Dock With Another Vessel](#dock-with-another-vessel) | Significant | Pilot |
| [Evasive Maneuvers](#evasive-maneuvers) | Significant | Pilot |
| [Line Up The Shot](#line-up-the-shot) | Significant | Pilot |
| [Pursuit](#pursuit) | Significant | Pilot |
| [Ram](#ram) | Significant | Pilot |
| [Electronic Warfare](#electronic-warfare) | Significant | Sensors Operator |
| [Intercept Enemy Communications](#intercept-enemy-communications) | Significant | Sensors Operator |
| [Maintain Communications](#maintain-communications) | Significant | Sensors Operator |
| [Sensor Targeting](#sensor-targeting) | Significant | Sensors Operator |

## Minor Actions

As in personal combat, minor actions are actions intended to perform tasks that do not require significant focus and concentration. Each crew member can take up to three minor actions per turn, at the loss of a significant action.

### Adjust Speed

The pilot may increase or decrease the vessel's speed up to an amount equal to its Thrust. This requires no skill check.

### Change Positions

Crew members changes crew positions, and are now considered to be manning their new position rather than their previous one.

### Maintain Course

The pilot keeps the vessel on its current course and heading, remaining at the current speed. This requires no skill check.

### Personal Action

A crew member may pursue any personal action that generally takes less than a minute.

### Miscellaneous

The Referee may permit a character to perform a skill check or other action as a minor action if the use of the skill does not require the character's full attention or complex physical actions.

## Significant Actions

Significant actions are intended to do something within about 3 seconds. You can perform a single significant action per turn, or forego it to perform a total of three minor actions.

### Take Minor Actions

A character can take two minor actions instead of a significant action.

### Attack

A gunner may attack any target within the range of the weapon system they are manning. The gunner attacks by making a [Turret Weapons](../book1/skills.md#turret-weapons) or [Bay Weapons](../book1/skills.md#bay-weapons) skill check roll at a Difficulty determined by range, adding any Computer Targeting, Sensor Targeting, or other modifiers in effect this round for his vessel. The enemy ship may react by dodging, point defense or triggering screens (see [Reactions](#reactions)). A gunner may fire any or all of the weapons in his turret or bay but each turret or bay may only fire once per round. If the attack is successful it will inflict damage. Damage is resolved after all attacks have been made in a vessel's turn. Missiles do not impact in the same round they are launched; their damage is resolved in the combat turn that they impact their target.

### Avoid Collision

When a vessel is moving at Short or Close range through a debris field, traffic, an asteroid belt, a planetary ring, or similar situation where there is a reasonable chance of collision with another object, the pilot must make an Avoid Collision check each turn. A collision inflicts 1D6 damage for every point of the vessel's current speed.

| Situation | Difficulty |
| --- | --- |
| Traffic (5 or more vessels within Short range), debris | Average (+0) |
| Asteroid field, light density | Difficult (-2) |
| Asteroid field, average density | Very Difficult (-4) |
| Asteroid field, heavy density | Formidable (-6) |
| Significant speed difference between ship and debris | DM -2 |

### Boarding Action

If two ships are Adjacent or docked, then a boarding action can be attempted. If the ships are docked, then the attackers may cross over safely via airlocks. If the ships are merely adjacent, then the attackers must use thruster packs or small craft to cross over. While crossing, the attackers may be attacked with point defense weapons or by firing sand. Once across, boarding actions can be resolved using the personal combat rules or the abstract boarding rules.

### Break Pursuit

If a vessel is being pursued (as per the Pursuit action) the pilot or driver may break the pursuit with a successful opposed [Pilot](../book1/skills.md#piloting) skill check against his opponent. Once the pursuit has broken and the pursuing vessel loses all accumulated attack bonuses against that target.

### Calculate Jump Plot

The navigator can hastily calculate a Jump Plot as a significant action in space combat. Normally an Easy (+4) Education-based [Navigation](../book1/skills.md#navigation) skill check, hastening it to fit within one space combat turn imposes a DM-1 on the skill check.

### Dock with Another Vessel

The pilot must make a successful Average(+0) [Piloting](../book1/skills.md#piloting) check. If the other ship does not wish to be docked with then make opposed [Piloting](../book1/skills.md#piloting) checks; the ship trying to dock suffers a –2 DM. When docked, boarding actions can take place.

### Coordinate Crew

The Captain makes an Average(+0) [Leadership](../book1/skills.md#leadership) skill check. The Captain gains a pool of points equal to the Effect of the skill check (minimum of 1), which he can distribute to individual crew members as DMs (granting a +1 DM per point) on skill or ability checks during the combat turn.

### Electronic Warfare

A sensors operator may attempt to jam radio communications and sensor locks by making an opposed Intelligence-based [Comms](../book1/skills.md#comms) check against the sensors operator of the opposing vessel. Electronic warfare can be used to break sensor locks.

Alternatively, electronic warfare can be used to attack smart missiles that are targeting the ship. The sensors operator makes a Difficult (–2) [Comms](../book1/skills.md#comms) check and, if successful, a single attacking smart missile ceases attacking. The sensors operator may continue making checks to disable smart missiles until he fails one, with a cumulative –1 DM each time.

### Evasive Maneuvers

The pilot operates the vessel in an erratic manner in an attempt to avoid being hit by opposing weapons fire. The Pilot makes an Average(+0) [Piloting](../book1/skills.md#piloting) skill check. If successful, any attack rolls targeting the pilot's vessel suffer a DM-1 penalty, or DM-2 with an Exceptional Success.

### Increase Initiative

The Captain of a vessel may make a [Leadership](../book1/skills.md#leadership) check and increase the Initiative of his vessel by the Effect of the check. This increase only applies for the following turn.

### Intercept Enemy Communications

The sensors operator may attempt to intercept enemy communications. This requires a Difficult(-2) [Comms](../book1/skills.md#comms) skill check. Encryption (if any) must also be broken. Knowledge of enemy intentions can be valuable to a Captain, if they know how to use it. The Captain may make an Easy (+4) [Tactics](../book1/skills.md#tactics) check to gain an advantage from intercepted communications. If successful, the Captain gains knowledge of the enemy's dispositions or intentions. This translates to a one-time DM+4 bonus to any skill check affecting the enemy (e.g. a pilot's attempt to evade a sudden attack, or a gunner's precise shot just as the enemy vessel turns to present a better target.) Of course, the enemy must have communications for them to be intercepted.

### Line Up the Shot

A pilot may attempt to aid his gunners by providing a stable firing platform along an optimum attack vector. The pilot makes a [Piloting](../book1/skills.md#piloting) check to aid his gunners, granting a DM+1 on all attacks rolls this turn with a success, or a DM+2 with an Exceptional Success.

### Maintain Communications

The sensors operator can establish and maintain communications between allied vessels with a Routine(+2) [Comms](../book1/skills.md#comms) skill check. If there is significant interference or a lot of communications going on (e.g. due to bad comm. discipline among a fleet), treat as Considerable Noise (DM-2). Deliberate comms jamming requires an opposed [Comms](../book1/skills.md#comms) skill check with the jammer. If reliable communications are not established, vessels cannot act in concert, and Tactics skill cannot be applied.

### Pursuit

If a pilot makes a successful opposed [Piloting](../book1/skills.md#piloting) skill check against another vessel within Short or Close range travelling at the same speed as the pilot's vessel, the successful pilot has placed his vessel in pursuit of his target maintaining the current range and matching the target ship move for move. Once a pursuit has been established, it must be maintained each turn to take advantage of the position. Maintaining a pursuit is a significant action that does not require a skill check. It is automatically maintained unless the target succeeds at the Break Pursuit action, manages to extend the range between the vessels to Medium or greater, or succeeds in outpacing the speed of the pursuing vessel by 7+ points. For each turn (after the first) that a vessel maintains pursuit of another vessel, it gains a cumulative DM+1 to hit when attacking the vessel being pursued, up to a maximum of DM+4.

### Ram

Ramming is a potentially suicidal maneuver in which a pilot intentionally crashes his vessel into the hull of another vessel. This action may only be attempted at Close range, and if the ramming vessel is moving faster than the target. To successfully ram another vessel requires an opposed [Piloting](../book1/skills.md#piloting) skill check between both vessels. A collision inflicts 1D6 damage for every point of difference in speed between the two vessels.

### Range Check

The Navigator makes an opposed [Navigation](../book1/skills.md#navigation) skill check with another vessel. The vessel with the highest result may elect to increase, decrease, or maintain the range between the vessels for the round.

### Reload Weapons System

A crew member (usually the gunner) may spend the round reloading one spent missile rack, one spent sandcaster or other individual weapon system.

### Repair Damaged System

A character on damage control may attempt to repair a damaged system by making an Education-based [Mechanics](../book1/skills.md#mechanics) check. If the check is successful, determine how many hits are repaired:

| Mechanics Check Effect | Hits Repaired |
| --- | --- |
| 0 | 1 |
| 1–5 | 2 |
| 6+ | 3 |

A ship with repair drones and the Auto-Repair software also makes one or two repair checks on the vessel's turn (unless it is being used to assist other repair attempts). The standard Auto-Repair software makes the check with a +1 DM. These are battlefield repairs only and will break down as soon as the battle is over unless repaired properly.

### Sensor Targeting

The sensors operator may attempt to spend the turn providing improved fire control and targeting data to the gunners, hopefully increasing their chances of hitting their targets. The sensors operator should make a Education-based [Comms](../book1/skills.md#comms) skill check against the target's Sensor Jamming rating, for each vessel that is to targeted. If successful, all gunners on the vessel gain a DM+1 bonus to their attacks this turn, or a DM+2 on an Exceptional Success. When using missiles the initial attack gets this bonus – the individual missile to hit rolls do not benefit directly. Smart missiles are unaffected.

### Miscellaneous

A character may make a skill check or do something else as a significant action when such an action requires the character's full attention, concentration, complicated physical actions or some combination thereof. Any skill check with a time interval of 1-6 minutes is simply considered a significant action during space combat.

## Reactions

As in [Personal Combat](personal-combat.md), reactions are actions taken immediately in response to the action of another. A ship may react to incoming attacks. The following situations allow reactions:

-   Targeted by a beam attack
-   Incoming missile
-   Attempted boarding

The ship's Initiative determines how many times it may react in a round.

| Initiative | Reactions |
| --- | --- |
| 0–4 | 1 |
| 5–8 | 2 |
| 9–12 | 3 |
| 13+ | 4 |

### Dodge Incoming Fire

To dodge, the pilot must make a [Piloting](../book1/skills.md#piloting) check. If successful, the attack suffers a –2 DM.

### Fire Sand

Turrets equipped with sandcasters can fire sand at incoming beam attacks. Each reaction spent on firing sand allows the gunner to make a [Turret Weapons](../book1/skills.md#turret-weapons) roll. If successful the damage of each beam in the incoming attack is reduced by 1D6. Resolve each beam separately. Each firing of sand costs one canister of sand. Sand can also be directed against incoming boarding parties. If the sand attack is successful, each target in the boarding party takes 8D6 damage.

### Point Defense

Turret lasers can be used to destroy incoming missiles. The missiles can only be destroyed in the moments before they strike the spacecraft as they are too small and fast-moving to effectively target at greater ranges. The gunner must make a [Turret Weapons](../book1/skills.md#turret-weapons) check against the missile. If successful, the missile is destroyed. A gunner may keep making [Turret Weapons](../book1/skills.md#turret-weapons) checks against missiles until he misses an attack; each attack suffers a cumulative –1 penalty. Attacks may be directed against different incoming missiles. Point defense can also be used to attack incoming boarders in the same way.

### Trigger Screens

Screens can be activated as long as the commander or one of the gunners has the [Screens](../book1/skills.md#screens) skill at Level 0 and the ship has the required screen type (nuclear against nuclear missiles and fusion guns; meson against meson guns). Screens reduce the damage from the attack by 2D6+the operator's [Screens](../book1/skills.md#screens) skill. Nuclear dampers also remove the automatic radiation hit from nuclear missile attacks.

## Other Actions

Other types of action can take place during space combat.

### Free Actions

Some actions are so fast on the scale of space combat that they do not even qualify as a minor action. A character can perform as many of these free actions as he likes in a turn, although if he performs several the Referee may require him to spend a minor or even a significant action on his various tasks.

### Extended Actions

Some skill checks will take longer than a single combat turn to complete. Make a Timing roll for the task and then work out how many six minute combat turns it will take to complete. A character engaging in an extended action cannot do anything else but can abandon their action at any time and return to the normal Initiative order. A character who is hit by an attack while undertaking an extended action must make an 8+ roll using the skill in question with a negative DM equal to the amount of damage the attack causes (after armor). Failure indicates that this turn's work does not count towards the completion of the task. Failure by six or more (an Exceptional Failure) ruins the task and the character must start again.

### Delay

A vessel does not have to act when its turn comes up in the Initiative order. The Captain may decide to act at any later point during the turn, even interrupting another's actions to do so. When he acts, his Initiative is set to the count on which he acted. If the character has not acted by the end of the turn he may choose to act first in the next turn, effectively giving up his actions in the previous turn in exchange for an Initiative advantage. His new Initiative is set to one higher than that of the current first person in the order. When multiple characters are delaying and all wish to act first in the following turn, their Initiatives are all set to the same score and they act in order of Thrust as normal.

## Special Considerations

The following are special considerations in space combat.

### Abstract Boarding Rules

In circumstances when the Referee may wish to resolve a boarding action without resorting to the personal combat rules, the following alternative method is suggested. On each round of a boarding action, the attacking Chief Security Officer (or Captain, if the CSO is down or not appointed) makes an opposed Intelligence-based [Tactics](../book1/skills.md#tactics) skill check against the Chief Security Officer (or Captain, if the CSO is down or not appointed) of the defenders. The results of each round depend on who wins and the degree of success achieved by the winner of the opposed check, as outlined in the Abstract Boarding Resolution table.

| Degree | Attacker Wins | Defender Wins |
| --- | --- | --- |
| Success | Defender loses reactions this round; Attacker gains DM+2 on next opposed Tactics roll for boarding actions; The ship suffers one Single Hit of internal damage. | Defender gains DM +2 on next opposed Tactics roll for boarding actions; The ship suffers one Single Hit of internal damage. |
| Exceptional Success | Attacker successfully boards ship (Defender crew may abandon ship, or are captured or killed at the discretion of the Attacker); Needs one turn to gain control of ship; Ship takes 2D6 damage of internal damage. | Attacker is driven back to their own ship or out into space (or captured or killed at the discretion of the Defender, if Attacker's forces are unable to retreat); If ships are still docked, Defender may elect to launch a boarding action against the former Attacker next turn. |

### Missiles

Unlike beam weapons, which travel at the speed of light and so hit the enemy vessel almost instantly, missile weapons take time to cross the gulf of space. Missiles travel at Thrust 10 towards their designated target and their position can either be tracked as additional craft in the battle or, for the sake of simplicity, they can be assumed to strike after a number of turns dependent on launch range, as shown in the Missile Launch Range table. Missiles cannot be used at Adjacent or Close range.

| Range | Turns to Impact |
| --- | --- |
| Adjacent | – |
| Close | – |
| Short | 1 |
| Medium | 1 |
| Long | 1 |
| Very Long | 2 |
| Distant | 2 |

When the missile is launched, the gunner must make a [Turret Weapons](../book1/skills.md#turret-weapons) or [Bay Weapons](../book1/skills.md#bay-weapons) skill check to determine the accuracy of the launch. The effect of the skill check determines the chance that the missile will strike its target when it hits. A target may react to incoming missiles by dodging or point defense. This reaction does not take place until the turn the missiles arrive at their destination, so any target response must wait until then.

| Turret Weapons/Bay Weapons check | Missile to-hit roll |
| --- | --- |
| Failed With Effect –6 or less | 11+ |
| Failed With Effect –1 to –5 | 10+ |
| Succeeded With Effect 0 | 8+ |
| Succeeded With Effect 1–5 | 7+ |
| Succeeded With Effect 6+ | 6+ |

#### Smart Missiles

The missile to-hit roll for smart missiles is always 8+ and if they miss they make another attack every turn until they are destroyed with point defense, jammed with ECM, run out of fuel or otherwise dissuaded.

### Planetary Maneuvers

Within close range of a planet, certain planetary maneuvers become possible.

Orbital Insertion

The pilot may attempt to insert the ship into orbit around a planet. In an orbital insertion fails, the ship fails to enter the proper orbit, and the orbit will steadily begin to decay drawing the ship towards the planet's atmosphere in an uncontrolled reentry (see Atmospheric Reentry below). This is a significant action requiring a Routine(+2) [Piloting](../book1/skills.md#piloting) skill check.

Atmospheric Entry

The pilot may attempt to transition the ship out of orbit and into the atmosphere of a planet. This is a significant action with an Average (+0) [Piloting](../book1/skills.md#piloting) skill check, with any applicable DMs from the Atmospheric Entry table. Exotic, Corrosive, or Insidious atmospheres should be treated as Standard unless specified otherwise.

| World Values | DM |
| --- | --- |
| World Size 9+ | \-2 |
| World Size 4 or less | +2 |
| World Atmosphere 1 or less | Auto Success |
| World Atmosphere 2-5, 14(E) | +2 |
| World Atmosphere 8-9, 13(D) | \-2 |

### Special Weapon Rules

Several types of weapons have their own rules.

**Meson Guns:** Meson guns ignore armor and always roll on the Internal Damage table. Furthermore, they also automatically inflict a radiation crew hit in addition to any other damage.

**Fusion Guns:** Fusion guns inflict a radiation crew hit in addition to any other damage. The bonus radiation hit suffers a –DM equal to the ship's armor.

**Particle Beams:** Particle beams inflict a radiation crew hit in addition to any other damage. The bonus radiation hit suffers a –DM equal to the ship's armor.

**Nuclear Missiles:** Nuclear missile hits inflict a radiation crew hit in addition to their normal damage. The bonus radiation hit suffers a –DM equal to the ship's armor.

**Sandcasters:** While the primary purpose of a sandcaster is to block incoming beam attacks, they can also be used as an attack. A sandcaster has a range of Close and inflicts 1 point of damage.

## Damage

Systems can take a variable number of hits before being destroyed, depending on the system in question. A ship can endure one point of Hull damage per fifty tons, rounding down. A ship that runs out of Hull Damage will rapidly be incapacitated. A ship can endure one point of Structure damage per fifty tons, rounding down to a minimum of one. A ship that runs out of Structure breaks up and is completely destroyed.

The effects of damage are determined by subtracting the ship's armor from the damage rolled by the weapon, then consulting the Space Combat Damage table to determine the number of hits inflicted. Then roll on the Space Combat Hit Location table for each hit. Small craft use the Small craft column. Vessels of 100 tons or larger use the External Hit (Vessel) column until a ship has suffered enough damage to wipe out its Hull, and then uses the Internal Hit (Vessel) column. A double hit applies two hits to the same location. A triple hit applies three hits to the same location.

| Damage | Effect |
| --- | --- |
| 0 or less | No damage |
| 1–4 | Single Hit |
| 5–8 | Two Single Hits |
| 9–12 | Double Hit |
| 12–16 | Three Single Hits |
| 16–20 | Two Single Hits, Double Hit |
| 21–24 | Two Double Hits |
| 24–28 | Triple Hit |
| 29–32 | Triple Hit, Single Hit |
| 33–36 | Triple Hit, Double Hit |
| 37–40 | Triple Hit, Double Hit, Single Hit |
| 41–44 | Two Triple Hits |
| For every extra three points | +1 Single Hit |
| For every extra six points | +1 Double Hit |

### Space Combat Hit Location

| 2D6 | External Hit (Vessel) | Internal Hit (Vessel) | Small Craft |
| --- | --- | --- | --- |
| 2 | [Hull](#hull) | [Structure](#structure) | [Hull](#hull) |
| 3 | [Sensors](#sensors) | [Power Plant](#power-plant) | [Power Plant](#power-plant) |
| 4 | [M-Drive](#m-drive) | [J-Drive](#j-drive) | [Hold](#hold) |
| 5 | [Turret](#turret) | [Bay](#bay) | [Fuel](#fuel) |
| 6 | [Hull](#hull) | [Structure](#structure) | [Hull](#hull) |
| 7 | [Armor](#armor) | [Crew](#crew) | [Armor](#armor) |
| 8 | [Hull](#hull) | [Structure](#structure) | [Hull](#hull) |
| 9 | [Fuel](#fuel) | [Hold](#hold) | [Turret](#turret) |
| 10 | [M-Drive](#m-drive) | [J-Drive](#j-drive) | [M-Drive](#m-drive) |
| 11 | [Sensors](#sensors) | [Power Plant](#power-plant) | [Crew](#crew) |
| 12 | [Hull](#hull) | [Bridge](#bridge) | [Bridge](#bridge) |

### Hull

Reduce the ship's Hull by one. If a ship's Hull is 0, then apply the hits to the location in the same row of the Internal Damage column.

### Structure

Reduce the ship's Structure by one. If a ship's Structure is reduced to 0, the ship is destroyed.

### Armor

Reduce the ship's armor by one. If the ship's armor is already 0, then this counts as a [Hull](#hull) hit.

### Turret

A random turret is hit.

-   **First Hit:** The turret's tracking mechanism is damaged. It can still be used, but all attacks suffer a –2 DM.
-   **Second Hit:** The turret and all weapons in it are disabled.
-   **Third Hit:** The turret and all weapons in it are destroyed.
-   **Subsequent Hits:** Count as [Hull](#hull) hits.

### Bay

A random bay is hit.

-   **First Hit:** The bay's targeting mechanism is damaged. It can still be used, but all attacks suffer a –2 DM.
-   **Second Hit:** The bay weapon is disabled.
-   **Third Hit:** The bay weapon is destroyed.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### J-Drive

The Jump drive is hit.

-   **First Hit:** All attempts at Jump suffer a –2 DM to Engineering checks.
-   **Second Hit:** The jump drive is disabled.
-   **Third Hit:** The jump drive is destroyed.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### M-Drive

The maneuver drive is hit.

-   **First Hit:** Reduce the ship's thrust by one.
-   **Second Hit:** Reduce the ship's thrust by 50%.
-   **Third Hit:** The drive is disabled.
-   **Subsequent Hits:** Count as [Hull](#hull) hits.

### Power Plant

The power plant is hit.

-   **First Hit:** Damaged.
-   **Second Hit:** The crew suffers a Crew Hit, rolling on the Radiation Damage column.
-   **Third Hit:** The Power Plant is destroyed and the ship is disabled.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### Sensors

The sensors are hit.

-   **First Hit:** –2 DM to all Comms checks to use sensors.
-   **Second Hit:** Sensors are disabled preventing the ship from making Comms checks for using sensors and on making attacks on targets beyond Adjacent range.
-   **Third Hit:** Sensors are destroyed.
-   **Subsequent Hits:** Count as [Hull](#hull) hits.

### Bridge

The bridge is hit.

-   **First Hit:** The crew suffers a Crew Hit, rolling on the Normal Damage column.
-   **Second Hit:** The bridge is disabled. Until the bridge is repaired, the ship cannot take any Pilot or Sensor actions, it cannot jump, and any attacks suffer a –2 DM.
-   **Third Hit:** The bridge is destroyed.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### Fuel

The fuel is hit.

-   **First Hit:** Causes a minor fuel leak of 1D6 tons per hour.
-   **Second Hit:** Destroys 1D6×10% of stored fuel.
-   **Third Hit:** Destroys fuel tank.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### Hold

The crago hold is hit.

-   **First Hit:** Destroys 1D6×10% of stored cargo.
-   **Second Hit:** Destroys 1D6×10% of stored cargo.
-   **Third Hit:** Destroys cargo hold and everything in it.
-   **Subsequent Hits:** Count as [Structure](#structure) hits.

### Crew

Each hit on the crew indicates that radiation or flying debris has injured one or more crew. Roll 2D6 on the appropriate column on the Crew Damage table.

| Roll | Normal Damage | Radiation Damage |
| --- | --- | --- |
| 4 or less | Lucky escape – no damage | Lucky escape – no radiation |
| 5–8 | One random crew member suffers 2D6 damage | One random crew member suffers 2D6×10 rads |
| 9–10 | One random crew member suffers 4D6 damage | One random crew member suffers 4D6×10 rads |
| 11 | All crew suffer 2D6 damage | All crew suffer 2D6×10 rads |
| 12 | All crew suffer 4D6 damage | All crew suffer 4D6×10 rads |

### Scaling Damage: Ship Weapons Against Personal-Scale Targets

Ship weapons suffer a DM -4 to hit a specific target on the personal combat scale. The Scaling Damage table covers the damage inflicted on the personal combat scale when ship weapons strike personal-scale targets.

| Ship Weapon | Personal Combat Damage |
| --- | --- |
| Beam Laser | 1D6×50 |
| Fusion Gun (bay) | 5D6×50 |
| Meson Gun (bay) | 5D6×50, plus 4D6×10 rads |
| Nuclear Missile | 2D6×50, plus 4D6×10 rads |
| Particle Beam (bay) | 6D6×50, plus 4D6×10 rads |
| Particle Beam (turret) | 3D6×50, plus 4D6×10 rads |
| Pulse Laser | 2D6×50 |
| Sandcaster | 8D6 |
| Smart Missile | 1D6×50 |
| Standard Missile | 1D6×50 |
