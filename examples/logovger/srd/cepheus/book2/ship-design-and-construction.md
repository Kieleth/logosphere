# Chapter 8: Ship Design and Construction

Space ships are the backbone of any starfaring campaign. This chapter provides rules for the design and construction of such vessels for use in Cepheus Engine campaigns. Any class A starport has a shipyard which can build any kind of ship, including a starship with Jump drives; any class B starport can build small craft and ships which do not have Jump drives.

## Standard Designs vs. New Designs

An interstellar economy provides an excellent opportunity for the use of standardized and modular designs. Components can be crafted on different worlds, taking advantage of available resources, and then put together to create a final product. Shipyards take advantage of modular components and standardized designs to reduce costs in production, which leads to a 10% discount on vessels constructed using common designs, such as those described in [Chapter 9: Common Vessels](common-vessels.md). The Referee may designate other ship designs as standard designs, as befits their universe. Fuel and weapon ammunition are not covered by the standard design discount.

New and unique ship designs cannot take advantage of standardized and modular design. These ships must be designed by a naval architect, who creates detailed design plans based on a set of specifications provided by their client. Such plans take a month to create, and costs approximately 1% of the final cost of the vessel.

## Ship Design Checklist

1.  [Choose a Ship Hull](#ship-hull)
    1.  [Determine hull configuration](#ship-configuration)
    2.  [Install armor (optional)](#ship-armor)
2.  [Choose maneuver drive (optional, but highly recommended)](#ship-drives)
3.  [Choose jump drive (optional)](#ship-drives)
4.  [Choose power plant](#ship-drives)
5.  [Determine fuel requirements](#fuel)
6.  [Determine bridge](#bridge)
7.  [Choose ship's computer](#ship-computer)
    1.  [Choose computer software](#ship-software)
8.  [Choose ship's electronics](#ship-electronics)
9.  [Determine number of required crew](#ship-crew)
    1.  Choose [staterooms](#staterooms) and [low berths](#low-passage-berths)
10.  [Determine additional features (optional)](#additional-ship-components)
1.  [Determine turrets, bays or screens (optional)](#armaments)
    1.  [Determine weapons (optional)](#armaments)
12.  Allocate remaining space to cargo
1.  Calculate final cost and construction time
    1.  Apply standard design discount of 10% (optional)

## A Note on System Redundancy

Ship systems take damage for a variety of reasons, becoming disabled or destroyed as damage accumulates. To counter the loss of vital ship systems, some ship designers install multiple versions of certain components. These redundant systems remain inactive until the original system is disabled, with the exception of ship weaponry. When all redundant systems have been disabled, any further damage begins to destroy them, beginning with the primary system.

## Displacement Tons

Hulls and other ship components are designated by their displacement volume. Displacement volume is measured in the volume of space that is displaced by one metric ton of hydrogen, referred to in this design sequence as displacement tons or simply tons.

A metric ton of hydrogen measures approximately 13.5 cubic meters, which is rounded to 14 cubic meters for ease of calculations. When drawing floor plans or maps of ships, each square measuring 1.5 meters by 1.5 meters, to a height of 3m up from the floor, represents half a ton.

## Ship Hull

The ship's hull is the shell in which all other components are placed. A ship's construction time is based on its hull size, as outlined on the Ship Hull table.

| Hull | Hull Code | Price (MCr) | Construction Time (weeks) |
| --- | --- | --- | --- |
| 100 tons | 1 | 2 | 36 |
| 200 tons | 2 | 8 | 44 |
| 300 tons | 3 | 12 | 52 |
| 400 tons | 4 | 16 | 60 |
| 500 tons | 5 | 32 | 68 |
| 600 tons | 6 | 48 | 76 |
| 700 tons | 7 | 64 | 84 |
| 800 tons | 8 | 80 | 92 |
| 900 tons | 9 | 90 | 100 |
| 1,000 tons | A | 100 | 108 |
| 1,200 tons | C | 120 | 124 |
| 1,400 tons | E | 140 | 140 |
| 1,600 tons | G | 160 | 156 |
| 1,800 tons | J | 180 | 172 |
| 2,000 tons | L | 200 | 188 |
| 3,000 tons | M | 300 | 268 |
| 4,000 tons | N | 400 | 348 |
| 5,000 tons | P | 500 | 428 |

### Ship Configuration

A ship may have any of three configurations – standard (a wedge, cone, sphere or cylinder), streamlined (a wing, disc or other lifting body allowing it to enter the atmosphere easily) or distributed (made up of several sections, and incapable of entering an atmosphere or maintaining its shape under gravity).

**Standard:** A standard-hull ship may still enter atmosphere but is very ungainly and ponderous, capable only of making a controlled glide to the surface. Getting it back into space requires an elaborate launch setup and considerable expense. A standard-hull ship may have scoops for gathering fuel from a gas giant but the process will be much more difficult and less efficient. Larger ships of this type will often carry a specialized sub-craft to perform the actual atmospheric skimming.

**Streamlined:** Streamlining a ship increases the cost of the hull by 10%. This streamlining includes fuel scoops which allow the skimming of unrefined fuel from gas giants or the gathering of water from open lakes or oceans. Streamlining may not be retrofitted; it must be included at the time of construction.

**Distributed:** A distributed ship reduces the cost of its hull by 10%. It is completely non-aerodynamic and if it enters an atmosphere or strong gravity it will fall to the surface of the planet. It cannot mount fuel scoops.

| Configuration | Hull Cost Modifier | Notes |
| --- | --- | --- |
| Distributed | ×0.9 | Cannot mount fuel scoops. Atmospheric operations suffer -4 DM (failed checks inflict 2D6 damage). |
| Standard | ×1 | Atmospheric operations suffer a -2 DM. |
| Streamlined | ×1.1 | Includes fuel scoops. |

### Ship Armor

Armor is added in 5% increments of the ship's tonnage. An armored ship decreases radiation exposure from space phenomena by 400 rads. (This does not apply to meson attacks and nuclear missiles, which bypass the armor or breach the hull to deliver their radiation hits.)

| Armor Type | TL | Protection | Cost |
| --- | --- | --- | --- |
| Titanium Steel | 7 | 2 per 5%, minimum 1 ton | 5% of base hull |
| Crystaliron | 10 | 4 per 5%, minimum 1 ton | 20% of base hull |
| Bonded Superdense | 14 | 6 per 5%, minimum 1 ton | 50% of base hull |

### Ship Armor Options

The following are options that can be added to a ship's armor.

**Reflec (TL 10):** Reflec coating on the hull increases the ship's armor against lasers by 3. Adding Reflec costs MCr0.1 per ton of hull and can only be added once.

**Self-Sealing (TL 9):** A self-sealing hull automatically repairs minor breaches such as micrometeoroid impacts, and prevents hull hits from leading to explosive decompression. It costs MCr0.01 per ton of hull.

**Stealth (TL 11):** A stealth coating absorbs radar and lidar beams, and also disguises heat emissions. This gives a –4 DM on any Comms rolls to detect or lock onto the ship. Adding Stealth costs MCr0.1 per ton of hull, and can only be added once.

### Hull and Structure

Initial damage is applied to the Hull; once the Hull is breached, further damage goes to the Structure. When all Structure Points have been lost, the ship has been smashed to pieces. A ship has one Hull Point per 50 tons of displacement (rounded down) and one Structure Point per 50 tons of displacement (rounded up).

## Ship Sections

Most vessels are divided into two primary sections.

### The Engineering Section

The Engineering section contains the drives and power plant necessary for proper operation and movement.

### The Main Compartment

The ship's main compartment contains all non-drive features of the ship, including the bridge, ship's computer, the staterooms, the low passage berths, the cargo hold and other items.

## Ship Drives

A non-starship must have a maneuver drive (M-Drive) and a power plant (P-Plant). A starship must have a Jump drive (J-Drive) and a power plant; a maneuver drive may also be installed, but is not required.

### Drive Costs

| Drive Code | J-Drive Tons | MCr | M-Drive Tons | MCr | P-Plant Tons | MCr |
| --- | --- | --- | --- | --- | --- | --- |
| A | 10 | 10 | 2 | 4 | 4 | 8 |
| B | 15 | 20 | 3 | 8 | 7 | 16 |
| C | 20 | 30 | 5 | 12 | 10 | 24 |
| D | 25 | 40 | 7 | 16 | 13 | 32 |
| E | 30 | 50 | 9 | 20 | 16 | 40 |
| F | 35 | 60 | 11 | 24 | 19 | 48 |
| G | 40 | 70 | 13 | 28 | 22 | 56 |
| H | 45 | 80 | 15 | 32 | 25 | 64 |
| J | 50 | 90 | 17 | 36 | 28 | 72 |
| K | 55 | 100 | 19 | 40 | 31 | 80 |
| L | 60 | 110 | 21 | 44 | 34 | 88 |
| M | 65 | 120 | 23 | 48 | 37 | 96 |
| N | 70 | 130 | 25 | 52 | 40 | 104 |
| P | 75 | 140 | 27 | 56 | 43 | 112 |
| Q | 80 | 150 | 29 | 60 | 46 | 120 |
| R | 85 | 160 | 31 | 64 | 49 | 128 |
| S | 90 | 170 | 33 | 68 | 52 | 136 |
| T | 95 | 180 | 35 | 72 | 55 | 144 |
| U | 100 | 190 | 37 | 76 | 58 | 152 |
| V | 105 | 200 | 39 | 80 | 61 | 160 |
| W | 110 | 210 | 41 | 84 | 64 | 168 |
| X | 115 | 220 | 43 | 88 | 67 | 176 |
| Y | 120 | 230 | 45 | 92 | 70 | 182 |
| Z | 125 | 240 | 47 | 96 | 73 | 192 |

### Drive Performance by Hull Volume, Smaller Hulls

|  | 100 | 200 | 300 | 400 | 500 | 600 | 700 | 800 | 900 | 1000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | 2 | 1 | – | – | – | – | – | – | – | – |
| B | 4 | 2 | 1 | 1 | – | – | – | – | – | – |
| C | 6 | 3 | 2 | 1 | 1 | 1 | – | – | – | – |
| D | – | 4 | 2 | 2 | 1 | 1 | 1 | 1 | – | – |
| E | – | 5 | 3 | 2 | 2 | 1 | 1 | 1 | 1 | 1 |
| F | – | 6 | 4 | 3 | 2 | 2 | 1 | 1 | 1 | 1 |
| G | – | – | 4 | 3 | 2 | 2 | 2 | 2 | 1 | 1 |
| H | – | – | 5 | 4 | 3 | 2 | 2 | 2 | 2 | 2 |
| J | – | – | 6 | 4 | 3 | 3 | 2 | 2 | 2 | 2 |
| K | – | – | – | 5 | 4 | 3 | 3 | 3 | 2 | 2 |
| L | – | – | – | 5 | 4 | 3 | 3 | 3 | 3 | 3 |
| M | – | – | – | 6 | 4 | 4 | 3 | 3 | 3 | 3 |
| N | – | – | – | 6 | 5 | 4 | 4 | 4 | 3 | 3 |
| P | – | – | – | – | 5 | 4 | 4 | 4 | 4 | 4 |
| Q | – | – | – | – | 6 | 5 | 4 | 4 | 4 | 4 |
| R | – | – | – | – | 6 | 5 | 5 | 5 | 4 | 4 |
| S | – | – | – | – | 6 | 5 | 5 | 5 | 5 | 5 |
| T | – | – | – | – | – | 6 | 5 | 5 | 5 | 5 |
| U | – | – | – | – | – | 6 | 6 | 5 | 5 | 5 |
| V | – | – | – | – | – | 6 | 6 | 6 | 5 | 5 |
| W | – | – | – | – | – | – | 6 | 6 | 6 | 5 |
| X | – | – | – | – | – | – | 6 | 6 | 6 | 6 |
| Y | – | – | – | – | – | – | 6 | 6 | 6 | 6 |
| Z | – | – | – | – | – | – | 6 | 6 | 6 | 6 |

### Drive Performance by Hull Volume, Larger Hulls

|  | 1200 | 1400 | 1600 | 1800 | 2000 | 3000 | 4000 | 5000 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | – | – | – | – | – | – | – | – |
| B | – | – | – | – | – | – | – | – |
| C | – | – | – | – | – | – | – | – |
| D | – | – | – | – | – | – | – | – |
| E | – | – | – | – | – | – | – | – |
| F | 1 | – | – | – | – | – | – | – |
| G | 1 | 1 | – | – | – | – | – | – |
| H | 1 | 1 | 1 | – | – | – | – | – |
| J | 2 | 1 | 1 | 1 | – | – | – | – |
| K | 2 | 2 | 1 | 1 | 1 | – | – | – |
| L | 2 | 2 | 2 | 1 | 1 | – | – | – |
| M | 3 | 2 | 2 | 2 | 1 | – | – | – |
| N | 3 | 3 | 2 | 2 | 2 | – | – | – |
| P | 3 | 3 | 3 | 2 | 2 | – | – | – |
| Q | 4 | 3 | 3 | 3 | 2 | 1 | – | – |
| R | 4 | 4 | 3 | 3 | 3 | 1 | – | – |
| S | 4 | 4 | 4 | 3 | 3 | 1 | – | – |
| T | 5 | 4 | 4 | 4 | 3 | 2 | – | – |
| U | 5 | 4 | 4 | 4 | 4 | 2 | – | – |
| V | 5 | 5 | 4 | 4 | 4 | 2 | 1 | – |
| W | 5 | 5 | 4 | 4 | 4 | 3 | 1 | 1 |
| X | 5 | 5 | 5 | 4 | 4 | 3 | 1 | 1 |
| Y | 5 | 5 | 5 | 4 | 4 | 3 | 2 | 1 |
| Z | 6 | 5 | 5 | 5 | 4 | 4 | 2 | 2 |

For maneuver drives, the potential is the Thrust number (Tn), which is the number of Gs acceleration available. For Jump drives, the potential is the Jump number (Jn), or Jump range in parsecs. The power plant rating (A-Z) must be at least equal to either the maneuver drive or Jump drive rating, whichever is higher.

### Fuel

Fuel needed for a Jump depends on the size of the ship and the length of the Jump and is calculated as 0.1 x Hull tonnage x Jump distance. A single Jump of that distance consumes that much fuel.

The amount of fuel required by the power plant depends on the tonnage of the power plant itself, and is calculated as one-third of the power plant tonnage per week, rounded down to the nearest ton. Space-faring vessels require a minimum of two weeks' worth of fuel for the power plant. Deep space vessels may store four, six or even eight weeks of power plant fuel. For your convenience, the Power Plant Fuel Requirements table provides calculated values for both the fuel per week and minimum fuel volume by Drive Code.

#### Power Plant Fuel Requirements

| Drive Code | P-Plant (tons) | Fuel/Wk (tons) | Min. Fuel Volume |
| --- | --- | --- | --- |
| A | 4 | 1 | 2 |
| B | 7 | 2 | 4 |
| C | 10 | 3 | 6 |
| D | 13 | 4 | 8 |
| E | 16 | 5 | 10 |
| F | 19 | 6 | 12 |
| G | 22 | 7 | 14 |
| H | 25 | 8 | 16 |
| J | 28 | 9 | 18 |
| K | 31 | 10 | 20 |
| L | 34 | 11 | 22 |
| M | 37 | 12 | 24 |
| N | 40 | 13 | 26 |
| P | 43 | 14 | 28 |
| Q | 46 | 15 | 30 |
| R | 49 | 16 | 32 |
| S | 52 | 17 | 34 |
| T | 55 | 18 | 36 |
| U | 58 | 19 | 38 |
| V | 61 | 20 | 40 |
| W | 64 | 21 | 42 |
| X | 67 | 22 | 44 |
| Y | 70 | 23 | 46 |
| Z | 73 | 24 | 48 |

## Bridge

The size of the bridge varies depending on the size of the ship. The cost for the ship's bridge is MCr0.5 per 100 tons of ship.

| Ship Size | Bridge Size |
| --- | --- |
| 200 tons or less | 10 tons |
| 300 tons – 1000 tons | 20 tons |
| 1,100 – 2000 tons | 40 tons |
| More than 2,000 tons | 60 tons |

## Ship Computer

| Computer | TL | Rating | Cost |
| --- | --- | --- | --- |
| Model 1 | 7 | 5 | Cr30,000 |
| Model 2 | 9 | 10 | Cr160,000 |
| Model 3 | 11 | 15 | MCr2 |
| Model 4 | 12 | 20 | MCr5 |
| Model 5 | 13 | 25 | MCr10 |
| Model 6 | 14 | 30 | MCr20 |
| Model 7 | 15 | 35 | MCr30 |

### Ship Computer Options

The following options are available for ship's computers.

**Jump Control Specialization (bis):** A computer's rating can be increased by 5 for the purposes of running Jump Control programs only. This increases the computer's cost by 50%.

**Hardened Systems (fib):** A computer and its connections can be hardened against attack by electromagnetic pulse weapons. A hardened system is immune to EMP, but costs 50% more.

Both options can be applied to the same computer by doubling its cost (+100%).

### Ship Software

Ship computers run highly specialized software packages designed to support numerous functions, such as managing the Jump drive, evading incoming fire, controlling ship's weapons and executing automatic repairs. Ship's computers automatically provide the means for basic control of the vessel, as well as extensive library data on numerous topics and a basic level of security (Security/0).

| Program | TL | Rating | Cost (MCr) | Notes |
| --- | --- | --- | --- | --- |
| Auto-Repair | 10+ | 10 per repair attempt | 5 per repair attempt | Computer may attempt one repair per turn, or give a DM+1 to an attempt; at TL 12, can purchase an additional repair attempt (or DM+1) for twice the Rating and Cost. |
| Evade | 9+ | 5 + 5 per DM-1 | 1 per DM-1 | Imposes DM-1 on incoming fire; every two TLs higher, can purchase an addition DM-1, to a maximum of DM-3 at TL 13. |
| Fire Control | 9+ | 5 per weapon | 2 per weapon | Ship's computer may fire one weapon; for each additional weapon, TL is increased by 1 (the ship can fire up to 2 weapons at TL 10, 3 at TL 11, and so on). The ship's computer may also sacrifice controlling a weapon to give a DM+1 from computer targeting on another attack, whether that weapon is controlled by the ship's computer or by an actual gunner. Maximum of five weapons may be controlled by this program. |
| Jump Control | 9+ | 5 per Jn | 0.1xJn | Governs Jump drives up to a given Jump number (Jn); TL is the same as the TL required for a given Jump number. |
| Jump Course Tape | 9+ | 1 per Jn | 0.001xJn | Provides jump plot from one specific world to a specific destination system. Price based on number of parsecs between worlds; TL is the same as the TL required for a given Jump number needed to cover that Jump. |

## Ship Electronics

A ship comes with a basic communications, sensor and emissions-control electronics suite, but more advanced systems can be installed. The Dice Modifier applies to jamming and counter-jamming attempts.

| System | TL | DM | Includes | Tons | Cost |
| --- | --- | --- | --- | --- | --- |
| Standard | 8 | –4 | Radar, Lidar | Included in bridge | Included in bridge |
| Basic Civilian | 9 | –2 | Radar, Lidar | 1 | Cr50,000 |
| Basic Military | 10 | +0 | Radar, Lidar, Jammers | 2 | MCr1 |
| Advanced | 11 | +1 | Radar, Lidar, Densitometer, Jammers | 3 | MCr2 |
| Very Advanced | 12 | +2 | Radar, Lidar, Densitometer, Jammers, Neural Activity Sensor | 5 | MCr4 |

**Radar/Lidar** detects physical objects. It can be active or passive. If a ship is using active sensors, it is easier to detect (+2 DM to Comms checks) but detects more about its surroundings.

**Jammers** can jam or counter-jam radio communications and sensor locks.

**Densitometers** can determine the internal structure and makeup of an object.

**Neural Activity Sensor** detects neural activity and intelligence.

## Ship Crew

All vessels require a crew to operate and maintain the ship. Small independently-owned vessels tend to operate with a minimum of crew, while corporate and military vessels maintain a full complement.

| Position | Minimum | Full Complement |
| --- | --- | --- |
| Command | None | One commanding officer or Captain, one executive officer, three administrative personnel (for ships over 1,000 tons) |
| Pilot | One | Three (one per 8-hour shift) |
| Navigator | One (optional with computer software) | One |
| Engineer | One | One per 35 tons of drives and power plant |
| Sensors Operator | None | One |
| Medic | None | One per 120 passengers and crew |
| Steward | None | One per four high passengers or ten middle passengers (assumes Steward-1) |
| Turret Gunner | One per turret weapon | One per turret weapon |
| Bay Gunner | One per bay weapon | Two per bay weapon |
| Screen Operator | One per screen device | Four per screen device |
| Chief Security Officer | None | One (optional) |
| Flight Crew | None | One per smallcraft or vehicle carried in hangars or launch tubes, plus one support crew per three vehicles or vessels |
| Marine | None | On ships over 1,000 tons, may have up to 30 per 1,000 tons |
| Other | None | As needed (i.e. medical staff, scientists, surveyors, etc.) |

### Staterooms

Each stateroom is sufficient for one person, displaces 4 tons, and costs Cr500,000. No stateroom can contain more than two persons, such as for middle passengers, as it would strain the ship's life support equipment. The tonnage and cost of the staterooms includes the life support systems needed to keep the crew alive.

### Low Passage Berths

One low passage berth carries one low passenger, costs Cr50,000, and displaces one-half ton.

Emergency low berths are also available; they will not carry passengers, but can be used for survival. Each costs Cr100,000 and displaces one ton. Each holds four persons.

### Barracks

A barracks takes up 2 tons per marine, and costs MCr0.1 per marine. Barracks can only be used to accommodate troops intended for boarding or assault operations. Troops accommodated in barracks cannot be used to reduce the number of service crew embarked.

## Additional Ship Components

The following are examples of additional ship components that might prove useful for certain ship designs.

### Armory

Ships carrying a large number of marines or soldiers can benefit from an armory, a specialized weapons store. An armory can only be accessed by those with the correct codes (usually the ship's senior officers and security team) and contains a wide variety of weapons. In game terms, an armory has enough snub pistols for the crew, enough accelerator or gauss rifles for any marines, and a selection of other military equipment like grenades, combat drug packs, combat armor and communications equipment. A general armory for a spacecraft costs MCr0.5 and takes up 2 tons of space.

Where military vessels are concerned, the number of armories built into the ship's design is based on crew size. One armory is installed for either every 50 crew members, or every 10 marines, in order to provide adequate storage for equipment, weapons and ammunition.

### Briefing Room

A specialized briefing room is useful on mercenary cruisers and other adventuring ships, where teams can discuss plans or meet with clients privately. A briefing room gives a +1 DM to Tactics checks made when planning missions on board ship. Ships with command bridges and fighter squadrons require additional briefing rooms and facilities. Capital ships must therefore have one briefing room per ship section, and one briefing room for every 20 fighter or bomber crew.

### Cargo Hold

The design plan must indicate cargo capacity. There is no cost but cargo carried may not exceed cargo capacity. Any space left over after all systems have been installed may be allocated to cargo space.

### Detention Cells

Found primarily on military and government vessels, a detention cell is used to keep prisoners. A detention cell displaces 2 tons and costs MCr0.25.

### Fuel Scoops

Fuel scoops allow an unstreamlined ship to gather unrefined fuel from a gas giant. Streamlined ships have fuel scoops built in. Adding scoops costs MCr1 and requires no tonnage.

### Fuel Processors

Fuel processors convert unrefined fuel into refined fuel. One ton of fuel processors can convert 20 tons of unrefined hydrogen into refined fuel per day. A ton of fuel processing equipment costs Cr50,000.

### Laboratory

Space allocated to laboratories can be used for research and experimentation. Each four tons of lab space allows for one scientist to perform research on board ship. The cost for research equipment varies depending on the type of research undertaken, but is generally around MCr1.0 per 4 tons.

### Launch Tubes

Launching and recovering small craft from a larger vessel is usually an activity taking 30 minutes to launch or recovery one craft. Launch tubes allow small craft to be launched and recovered rapidly from a ship. The size of a launch tube is twenty–five times the tonnage of the largest craft that will be deployed in this manner, and they cost MCr0.5 per ton. With a launch tube, up to ten small craft can be launched per round. Multiple launch tubes can be installed.

### Library

A library room contains computer files as well as lecterns, display screens, holotanks and even hard copies of books. A good library is useful for both research and passing time in jump space. Having a library on board a ship gives one extra week of training time for new skills per week spent in jump space. A library for a spacecraft costs MCr4 and takes up 4 tons of space.

### Luxuries

Luxuries cost Cr100,000 per ton, and make life on board ship more pleasant. Each ton of luxuries counts as one level of the Steward skill for the purposes of carrying passengers, and therefore allows a ship to carry middle and high passage passengers without carrying a trained steward on board.

### Ship's Locker

Every ship has a ship's locker. Typical equipment carried aboard will include protective clothing, vacc suits, weapons such as shotguns or pistols, ammunition, compasses and survival aids, and portable shelters. The contents of the locker are defined only when they need to be but always contains vacc suits and other useful items. The ship's locker is usually protected by a biometric lock keyed to the ship's officers.

### Vault

A vault is a special armored chamber in the heart of a spacecraft, designed to survive attacks that would annihilate the rest of the ship. A vault has another four Hull and Structure points that only come into play when the ship housing the vault is destroyed. A vault can contain cargo, staterooms or any other internal components equivalent up to 6 tons. A vault requires 12 tons of space and costs MCr6.

### Vehicle and Drone Hangar

The tonnage and cost outlined in the Hangar Cost and Tonnage for Vehicles and Drones table covers full-scale hangar space, which allows for repairs and maintenance of small craft when they are back on the ship. The hangar includes spare parts and specialized testing and repair equipment for the stored craft. It does not include the cost of the vehicles or drones. A custom hangar takes up tonnage equal to the tonnage of the vehicle to be stored, plus 30% and costs MCr0.2 per ton.

**Air/Raft, ATV:** These are vehicles, stored in or on the ship.

**Escape Pods:** This covers the installation of rescue bubbles and other escape pods for the entire crew.

**Life Boat, Ship's Boat, Shuttle, Pinnace, Cutter:** These are all small craft, hangered either in or on the ship's hull.

**Mining Drones:** Mining drones allow a ship to mine asteroids. Each set of mining drones takes up ten tons, and allows the ship to process 1D6×10 tons of asteroid per working day. The tonnage allocated includes ore handling machinery, allowing the ship to take on ore and transfer it to the cargo bay.

**Probe Drones:** Probe drones are for surveying planetary surfaces. Each ton of probe drones contains five drones. Probe drones can be dropped from orbit in disposable entry shells but must be recovered manually. Probe drones are also capable of surveying orbiting satellites, derelicts and other space debris. They can also be used as communications relays.

**Repair Drones:** Carrying repair drones allows a ship to make battlefield repairs with the AutoRepair software or when managed by a character with Mechanic or Engineer skills. Repair drones have the same statistics as repair robots only without an Intellect program. For more information on repair robots, see [Robots and Drones](../book1/equipment.md#robots-and-drones) in [Chapter 4: Equipment](../book1/equipment.md).

| Vehicle or Drone | Tons | Installation Cost (MCr) |
| --- | --- | --- |
| ATV | 13 | 2.6 |
| Air/Raft | 5 | 1 |
| Cutter | 65 | 13 |
| Escape Pods | 0.5 per passenger | 0.1 per passenger |
| Life Boat | 26 | 5.2 |
| Mining Drones | 10 | 2 |
| Pinnace | 52 | 10.4 |
| Probe Drones (5) | 1 | 0.2 |
| Repair Drones | 1% of ship's hull | 0.2 per ton |
| Ship's Boat | 39 | 7.8 |
| Shuttle | 122.5 | 24.5 |

## Armaments

A ship has one hardpoint per 100 tons of ship and each weapon system takes up one hardpoint. A weapon system may include multiple weapons – for example, a triple turret contains three lasers, missile launchers, sandcasters or some combination of three weapons.

### Turrets

One turret may be attached to each hardpoint on the ship. If a turret is installed, then one ton of space must be allocated to fire control systems:

| Weapon | TL | Tons | Cost (MCr) |
| --- | --- | --- | --- |
| Single Turret | 7 | 1 | 0.2 |
| Double Turret | 8 | 1 | 0.5 |
| Triple Turret | 9 | 1 | 1 |
| Pop-up Turret | 10 | 2 | +1 |
| Fixed Mounting | \- | 0 | ×0.5 |

**Single, Double and Triple turrets** can hold one, two or three weapons.

**Pop-Up** is a quality that can be applied to any type of turret – the turret is concealed in a pod or recess on the hull, and is detectable only when deployed. A ship with all its weapons in pop-up turrets looks unarmed to a casual sensor scan.

**Fixed Mounting** weapons cannot move, are limited to firing in one direction (normally straight ahead), and are found mainly on fighters. A fixed mounting costs half as much as a turret of the same type, so a single fixed mounting costs MCr0.1, a double fixed mounting costs MCr0.25, and a triple fixed mounting costs MCr0.5.

| Weapon | TL | Optimum Range | Damage | Cost (MCr) | Notes |
| --- | --- | --- | --- | --- | --- |
| Missile Rack | 6 | Special | Depends on missile | 0.75 |  |
| Pulse Laser | 7 | Short | 2D6 | 0.5 | Suffers DM-2 to attac |
| Sandcaster | 7 | Special | Special | 0.25 |  |
| Particle Beam | 8 | Long | 3D6 + radiation hit | 4 |  |

**Missile racks** launch self-propelled weapons designed to explode on impact.

**Pulse lasers** fire short, rapid bursts of intense energy. Pulse lasers are notoriously inaccurate and suffer a DM -2 on all attack rolls.

**Sandcasters** reduces the damage from a beam weapon by 1D6. Sandcasters require ammunition. Twenty sandcaster barrels take up one ton of space, can be manufactured at TL5, and cost Cr10,000.

**Particle beams** fire a high-energy beam of subatomic particles. The beam's impact disrupts the molecular structure of the target, causing a radiation crew hit in addition to normal damage.

**Beam lasers** fire a continuous stream of intense energy.

No launcher includes ammunition in its purchase cost. Missiles, torpedoes and so forth must be purchased separately.

### Missiles

Missiles are weapons that are self-propelled or directed by remote control, carrying a conventional or nuclear explosive. They may be fired from missile racks mounted in turrets or from bay-mounted missile banks. Twelve missiles take up one ton of space. Missiles are capable of Thrust 10, but have a limited endurance of 60 minutes (roughly 4 turns) before running out of fuel. There are three common types of missiles: Standard, Smart and Nuclear.

| Missile Type | TL | Damage | Cost per Missile | Notes |
| --- | --- | --- | --- | --- |
| Nuclear | 6 | 2D6+ 1 radiation hit | Cr3,750 | Radiation hit suffers a –DM equal to the ship's armor |
| Standard | 6 | 1D6 | Cr1,250 |  |
| Smart | 8 | 1D6 | Cr2,500 | Attack roll is always 8+, and may attack every turn if they miss until they are destroyed, jammed or run out of fuel |

### Bays

Bay weapons are much larger than turrets, and take up 50 tons of space and one hard point, as well as one ton of space for fire control.

| Weapon | TL | Range | Damage | Cost (MCr) |
| --- | --- | --- | --- | --- |
| Missile Bank | 6 | Special | Launches a flight of twelve missiles | 12 |
| Particle Beam | 8 | Long | 6D6 + 1 radiation hit | 20 |
| Meson Gun | 11 | Long | 5D6 + 1 radation hit | 50 |
| Fusion Gun | 12 | Medium | 5D6 | 8 |

**Missile banks** fire flights of twelve missiles at a time.

**Particle beam bays** fire a larger and more powerful beam of subatomic particles than particle beam turret weapons.

**Meson weapons** are unaffected by armor, as the blast only becomes harmful after it has already passed through the hull. Meson guns also inflict an automatic radiation hit on the crew of any target struck.

**Fusing weapons** fire a stream of hydrogen particles that are undergoing a fusion reaction.

### Screens

Screens are defensive systems that protect against specific attacks.

| Screen | TL | Effect | Tons | Cost (MCr) |
| --- | --- | --- | --- | --- |
| Meson Screen | 12 | Protects against meson weapon damage, reducing damage by 2D6. Meson screens reduce radiation damage from meson guns and meson flicker weapons. Radiation hits from these weapons suffer a –DM equal to twice the active number of screens. | 50 | 60 |
| Nuclear Damper | 12 | Reduces fusion gun damage and nuclear missile damage by 2D6, removes automatic radiation hit from nuclear missile attacks | 50 | 50 |

**Meson screens** block attacks from meson weapons by preventing meson decay.

**Nuclear dampers** inhibit fusion reactions, reducing the damage from fusion weapons and nuclear missiles by 2D6 when affected.

## Universal Ship Description Format

After a ship design has been created, it must be presented in a format that allows players to use the information within the game. The Cepheus Engine describes ship designs using a universal ship description format, which is essentially a paragraph of text laid out in the following manner. Examples can be found in [Chapter 9: Common Vessels](common-vessels.md).

> \[Ship's Tech Level\] \[Ship Descriptive Name\]  
Using a \[Ship Hull Displacement\]\-ton hull (\[Hull Damage Value\] Hull, \[Structure Damage Value\] Structure,), the \[Ship Descriptive Name\] is \[General Description of Ship's Function\]. It mounts jump drive \[Jump Drive Code\], maneuver drive \[Maneuver Drive Code\], and power plant \[Power Plant Code\], giving a performance of Jump-\[Jump Number\] and \[Thrust Number\]\-G acceleration. Fuel tankage of \[Fuel Tonnage\] tons supports the power plant for \[Weeks of Power\] and \[Number of Jumps\] jump-\[Jump Number\]. \[Any additional fuel usage notes.\] Adjacent to the bridge is a computer Model \[Computer Number, followed by a slash and bis or fib options noted, if purchased\]. The ship is equipped with \[Sensors Type\] sensors (\[Sensors DM\].) There are \[Number of Staterooms\] staterooms and \[Number of Low Berths\] low berths. The ship has \[Number of Hardpoints\] hardpoints and \[Fire Control Tonnage\] tons allocated for fire control. Installed on the hardpoints are \[Describe number and type of turrets, and any weapon systems that have been installed, if any. Also note any ammunition carried for missiles and sandcasters.\] This ship has \[Number of Screens Installed\] screens: \[Describe number and type of screens\]. There are \[Number of Small Craft Hangers\] small craft hangars, \[Describe number and contents of each hangar\]. Cargo capacity is \[Cargo Tonnage\] tons. The hull is \[Hull Configuration\], and is armored with \[Armor Type\] (\[Armor Rating\] points.) \[Note any Ship's Armor options that have been installed.\] Special features include \[List additional components here, included fuel processors and fuel scoops\]. The ship requires a crew of \[Crew Total\]: \[List crew positions\]. The ship can carry up to \[Double the Number of Non-Crew Staterooms\] additional passengers at double occupancy and \[Number of Low Berths\] low passengers. The ship costs MCr\[Cost of Ship\] (including discounts and fees) and takes \[Construction Time\] weeks to build.

## Alternative Star Drives

In classic era science-fiction games such as the Cepheus Engine, the only form of faster-than-light movement is typically the Jump drive, which always takes one week to travel a number of parsecs equal to its Jump rating and consumes a vast amount of fuel. If the Referee wishes to model other science fiction settings with their own forms of "star drive", the classic Jump drive rules may not be entirely appropriate. The alternative drives below use all the same rules as the Jump drive (mass, fuel, power consumption, range) unless otherwise stated.

Some of these drives consume much less fuel or allow much faster travel than the Jump drive, so introducing these drives will vastly impact the carrying capacity of a starship, the profitability of trade, the speed of communication and so forth.

### Warp Drive

The ship warps space around it, allowing it to move faster-than-light while staying in our universe. A warp drive does not have a maximum range – instead, the ship's drive rating indicates the number of parsecs crossed per week of travel. Warp travel consumes fuel at twice the normal rate for the ship's power plant rather than needing a single massive expenditure in the manner of a Jump drive.

### Teleport Drive

The ship instantaneously jumps from one point to another. This works just like the standard Jump drive without the week-long wait in hyperspace. Instead, no time whatsoever elapses during the transition from one place to another. A teleport consumes no extra fuel but jumping is a strain on the ship's systems and multiple successive jumps can damage the drive.

### Hyperspace Drive

The portal drive functions by opening up a gateway into hyperspace, through which the ship can pass. When in hyperspace, the ship uses its conventional engines to travel, then opens up a second gateway back to the normal universe, effectively taking a short cut through a higher dimension. A hyperspace drive is limited by the size of the spacecraft that can pass through the portal – see the Hyperspace Portal Size table. A hyperspace drive consumes no extra fuel, but takes up twice as much space as a jump drive. While in hyperspace, the spacecraft moves at a rate of one parsec per day per maneuver drive rating.

| Rating | Size | Rating | Size |
| --- | --- | --- | --- |
| A | 200 | N | 2800 |
| B | 400 | P | 3000 |
| C | 800 | Q | 3200 |
| D | 1000 | R | 3400 |
| E | 1200 | S | 3600 |
| F | 1400 | T | 3800 |
| G | 1600 | U | 4000 |
| H | 1800 | V | 4200 |
| J | 2000 | W | 4400 |
| K | 2200 | X | 4600 |
| L | 2400 | Y | 4800 |
| M | 2600 | Z | 5000 |

## Alternative Power Plants

The Cepheus Engine system posits the development of highly efficient fusion power plants, but other settings may use different sources of power. Unless otherwise noted, these power plants use all the same rules as the standard fusion power plants.

### Fission Plants

A fission plant requires radioactive elements as fuel. Fission drives only produce the same amount power as a fusion drive of the same type. However, they are twice the size and price of a fusion power plant. Fission plant fuel costs Cr1,000,000 per ton. Fission plants use the same amount of fuel in a year that standard fusion power plants require for two weeks.

### Antimatter Power Plants

Antimatter power plants work by annihilating small amounts of hydrogen and anti-hydrogen. No tonnage needs to be allocated to fuel, but the plant must be refueled once per month, at a cost of Cr5,000 per ton of drive.

## Small Craft Design

Small craft design follows the rules for standard ship design, with the following changes.

### Small Craft Ship Hull

Small craft use the Small Craft Ship Hull by Displacement table below. The cost of streamlining and distributed configurations are calculated as per standard ship design rules.

| Hull | Hull Code | Price (MCr) | Construction Time (weeks) |
| --- | --- | --- | --- |
| 10 tons | s1 | 1.1 | 28 |
| 15 tons | s2 | 1.15 | 29 |
| 20 tons | s3 | 1.2 | 29 |
| 25 tons | s4 | 1.25 | 30 |
| 30 tons | s5 | 1.3 | 30 |
| 35 tons | s6 | 1.35 | 30 |
| 40 tons | s7 | 1.4 | 31 |
| 45 tons | s8 | 1.45 | 31 |
| 50 tons | s9 | 1.5 | 32 |
| 55 tons | sA | 1.55 | 32 |
| 60 tons | sB | 1.6 | 32 |
| 65 tons | sC | 1.65 | 33 |
| 70 tons | sD | 1.7 | 33 |
| 75 tons | sE | 1.75 | 34 |
| 80 tons | sF | 1.8 | 34 |
| 85 tons | sG | 1.85 | 34 |
| 90 tons | sH | 1.9 | 35 |
| 95 tons | sJ | 1.95 | 35 |

### Small Craft Armor

Small craft armor is purchased in the same manner as standard ship armor. However, small craft have a maximum armor value, based on type. Armor options may be purchased as per the standard ship design rules.

| Armor Type | Maximum Armor Value |
| --- | --- |
| Titanium Steel | TL or 9, whichever is less |
| Crystaliron | TL or 13, whichever is less |
| Bonded Superdense | TL |

### Small Craft Drives

A small craft must have a maneuver drive (M-Drive) and a power plant (P-Plant). The costs for each are captured in the Small Craft Drive Costs table. The performance of small craft drives are found in the Small Craft Drive Performance by Hull Volume table.

#### Small Craft Drive Costs

| Drive Code | M-Drive Tonnage | MCr | P-Plant Tonnage | MCr |
| --- | --- | --- | --- | --- |
| sA | 0.5 | 1 | 1.2 | 3 |
| sB | 1 | 2 | 1.5 | 3.5 |
| sC | 1.5 | 3 | 1.8 | 4 |
| sD | 2 | 3.5 | 2.1 | 4.5 |
| sE | 2.5 | 4 | 2.4 | 5 |
| sF | 3 | 6 | 2.7 | 5.5 |
| sG | 3.5 | 8 | 3 | 6 |
| sH | 4 | 9 | 3.3 | 6.5 |
| sJ | 4.5 | 10 | 3.6 | 7 |
| sK | 5 | 11 | 3.9 | 7.5 |
| sL | 6 | 12 | 4.5 | 8 |
| sM | 7 | 14 | 5.1 | 9 |
| sN | 8 | 16 | 5.7 | 10 |
| sP | 9 | 18 | 6.3 | 12 |
| sQ | 10 | 20 | 6.9 | 14 |
| sR | 11 | 22 | 7.5 | 16 |
| sS | 12 | 24 | 8.1 | 18 |
| sT | 13 | 26 | 8.7 | 20 |
| sU | 14 | 28 | 9.3 | 22 |
| sV | 15 | 30 | 9.9 | 24 |
| sW | 16 | 32 | 10.5 | 26 |

#### Small Craft Drive Performance by Hull Volume

| Drive Code | 10 | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 | 55 | 60 | 65 | 70 | 75 | 80 | 85 | 90 | 95 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sA | 2 | 1 | 1 | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — |
| sB | 4 | 2 | 2 | 1 | 1 | 1 | 1 | — | — | — | — | — | — | — | — | — | — | — |
| sC | 6 | 4 | 3 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | — | — | — | — | — | — | — |
| sD | — | 5 | 4 | 3 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | — | — | — |
| sE | — | 6 | 5 | 4 | 3 | 2 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| sF | — | — | 6 | 4 | 4 | 3 | 3 | 2 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| sG | — | — | — | 5 | 4 | 4 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 |
| sH | — | — | — | 6 | 5 | 4 | 4 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 1 | 1 | 1 |
| sJ | — | — | — | — | 6 | 5 | 4 | 4 | 3 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 2 | 1 |
| sK | — | — | — | — | 6 | 5 | 5 | 4 | 4 | 3 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 2 |
| sL | — | — | — | — | — | 6 | 6 | 5 | 4 | 4 | 4 | 3 | 3 | 3 | 3 | 2 | 2 | 2 |
| sM | — | — | — | — | — | — | — | 6 | 5 | 5 | 4 | 4 | 4 | 3 | 3 | 3 | 3 | 2 |
| sN | — | — | — | — | — | — | — | — | 6 | 5 | 5 | 4 | 4 | 4 | 4 | 3 | 3 | 3 |
| sP | — | — | — | — | — | — | — | — | — | 6 | 6 | 5 | 5 | 4 | 4 | 4 | 4 | 3 |
| sQ | — | — | — | — | — | — | — | — | — | — | 6 | 6 | 5 | 5 | 5 | 4 | 4 | 4 |
| sR | — | — | — | — | — | — | — | — | — | — | — | 6 | 6 | 5 | 5 | 5 | 4 | 4 |
| sS | — | — | — | — | — | — | — | — | — | — | — | — | 6 | 6 | 6 | 5 | 5 | 5 |
| sT | — | — | — | — | — | — | — | — | — | — | — | — | — | 6 | 6 | 6 | 5 | 5 |
| sU | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | 6 | 6 | 5 |
| sV | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | 6 | 6 |
| sW | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | 6 |

### Small Craft Fuel

The fuel requirements for a small craft's power plant follow the standard ship design rules, except the values are rounded down to the nearest 0.1 of a ton, rather than the nearest ton. In addition, the minimum fuel volume for a small craft is one week rather than two weeks. For your convenience, the Small Craft Power Plant Fuel Requirements table provides calculated values for fuel per week by Drive Code.

| Drive Code | P-Plant (tons) | Fuel/Wk (tons) |
| --- | --- | --- |
| sA | 1.2 | 0.4 |
| sB | 1.5 | 0.5 |
| sC | 1.8 | 0.6 |
| sD | 2.1 | 0.7 |
| sE | 2.4 | 0.8 |
| sF | 2.7 | 0.9 |
| sG | 3 | 1 |
| sH | 3.3 | 1.1 |
| sJ | 3.6 | 1.2 |
| sK | 3.9 | 1.3 |
| sL | 4.5 | 1.5 |
| sM | 5.1 | 1.7 |
| sN | 5.7 | 1.9 |
| sP | 6.3 | 2.1 |
| sQ | 6.9 | 2.3 |
| sR | 7.5 | 2.5 |
| sS | 8.1 | 2.7 |
| sT | 8.7 | 2.9 |
| sU | 9.3 | 3.1 |
| sV | 9.9 | 3.3 |
| sW | 10.5 | 3.5 |
| sX | 11.1 | 3.7 |
| sY | 11.7 | 3.9 |
| sZ | 12.3 | 4.1 |

### Small Craft Cockpits and Control Cabins

Small craft do not have bridges. Instead, a cockpit or control cabin serves the same function. A cockpit is more cramped, but takes up less tonnage. The cost for a cabin or cockpit is the same – MCr 0.1 per 20 tons of ship. Additional cabin space costs MCr 0.05 per ton. Cockpits and control cabins come equipped with a basic communications, sensor and emissions-control electronics suite. More advanced systems can be installed per the standard ship design rules.

| Small Craft Size | Size (tons) | Crew |
| --- | --- | --- |
| 1-Man Cockpit | 1.5 | 1 crew |
| 2-Man Cockpit | 3 | 2 crew |
| 1-Man Control Cabin | 3 | 1 crew |
| 2-Man Control Cabin | 6 | 2 crew, 1 passenger |
| More Cabin Space | 1.5 tons per passenger | 1 additional passenger |

### Airlock

Unlike larger vessels, a small craft does not have an airlock by default. Airlocks take up one ton each and cost MCr0.2. If a craft does not have an airlock, then the crew cannot leave the craft except when it is landed or in a pressurized landing bay without opening the ship up to vacuum.

### Small Craft Crew

All small craft of 50 tons or under require a minimum crew of one to operate and maintain the ship. Small craft larger than 50 tons require a minimum crew of two.

### Small Craft Armaments

A small craft has one hardpoint, despite being less than 100 tons. Small craft follow the standard ship design rules regarding armaments, with some exceptions.

Meson, particle beam and fusion bays cannot be fitted.

The armaments allowed to a small craft are restricted by its power plant type. It may only equip up to the number of lasers and particle weapons allowed by the vessel's power plant, as shown in the Maximum Energy Weapons by Power Plant table. The number of missile launchers or projectile weapons is not limited by the power plant letter.

| Drive Code | Maximum Number |
| --- | --- |
| sA–sF | 0 |
| sG–sK | 1 |
| sL–sR | 2 |
| sS–sZ | 3 |
