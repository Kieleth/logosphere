# Chapter 12: Worlds

The basic planetary characteristics are Size, Atmosphere, Hydrology, Population, Government, Law Level, Technology Level, Starport and Bases, and are generated using two-dice throws, with DMs applied based on other characteristics. These characteristics establish the basic identity of a world, and are referred to as the Universal World Profile (UWP). Additional information can be generated, and should be, to more fully describe a world.

## The Universal World Profile (UWP)

The Cepheus Engine utilizes a concise one-line coding to encapsulate data on an individual world in a manner that, with a little practice, can be quickly and easily read. The specifics of the Universal World Profile can be found below:

```
WorldName   0000 A123456-7x Ni              R 123 Na
```

### The Explanation

**WorldName:** indicates the common name for the world that is being profiled.

**0000:** provides the location of the world's hex (column, then row) on the sector or subsector map.

**A123456-7:** is the classic world profile. Each number or letter is a pseudo-hexadecimal code representing a specific value on the corresponding world data charts. In order, the profile defines the following elements: [Starport](#primary-starport), [World Size](#world-size), [Atmosphere](#atmosphere), [Hydrographics](#hydrographics), [Population](#world-population), [Government](#world-government), [Law Level](#law-level), followed by a hyphen and finally [Technology Level](#technology-level).

**X:** indicates where information about a world's [bases](#bases) are noted. A space here indicates that the world has no bases worthy of note on an interstellar level.

**Ni:** is used here to indicate where special remarks and [trade codes](#trade-codes) are displayed as part of the world data profile.

**R:** provides information about the world's [Travel Zone](#travel-zones) classification. A space indicates a world that is generally safe to visit. An "A" represents an [Amber Zone](#amber-zone), indicating a world that adventurers should approach with more caution than normal. An "R" indicates a [Red Zone](#red-zone), a world where travel is prohibited for any of a number of reasons, from physical dangers to political secrecy.

**123:** represents a brief synopsis of three pieces of data: a [Population Modifier](#population-modifier) for the main world, the number of [Planetoid Belts](#planetoid-belt-presence) in the system, and the number of [Gas Giants](#gas-giant-presence) in the system.

**Na:** indicates the system's [interstellar allegiance](#polities-and-world-allegiance). "Na" is used for non-aligned worlds.

## Star Mapping

For Cepheus Engine universes, the presence of star systems is marked on hex maps, each hex representing one parsec. For each system, generate a Universal World Profile for the primary world of the system. The smallest astrogation map size, the subsector, measures 8 hexes wide by 10 hexes high. An intermediate map size, the quadrant, measures two subsectors by two subsectors, while the largest map size, the sector, measures two quadrants by two quadrants.

There is a basic one-half chance normally that a world (and its attendant stellar system) will be in a hex. Systematically check each hex on the subsector map, throwing one die and marking the hex with a circle if the result is a 4, 5, or 6. This indicates that a world is present; otherwise, leave the hex blank. The Referee may elect to alter the normal chances of worlds, making them more frequent or less frequent to correspond to specific regions of the galaxy. A 50% density (no DM) is appropriate for the spiral arms of the galaxy. Apply a –2 DM for 'rift sectors', a –1 DM for sparse sectors and a +1 DM for densely populated sectors.

## World Size

The Size characteristic for inhabitable worlds ranges from 0 to 10, and is determined by rolling 2D6–2.

| Digit | World Size | Surface Gravity (gs) |
| --- | --- | --- |
| 0 | 800 km (typically an asteroid) | Negligible |
| 1 | 1,600 km | 0.05 |
| 2 | 3,200 km | 0.15 |
| 3 | 4,800 km | 0.25 |
| 4 | 6,400 km | 0.35 |
| 5 | 8,000 km | 0.45 |
| 6 | 9,600 km | 0.7 |
| 7 | 11,200 km | 0.9 |
| 8 | 12,800 km | 1.0 |
| 9 | 14,400 km | 1.25 |
| 10 (A) | 16,000 km | 1.4 |

### High and Low Gravity Worlds

Worlds where the gravity is 0.75 or less are low-gravity worlds. Common features include improbable-looking rock formations, thin and spindly life forms and flying as a common form of locomotion (assuming the atmosphere is thick enough to support flyers). Humans tend to find life on low-gravity worlds to be initially pleasant, but regular exercise regimes and medicinal supplements are required to prevent bone and muscle degradation. Those who spent too long on low-gravity worlds cannot tolerate higher gravities. Characters on low-gravity worlds suffer a –1 DM to all skill checks until they acclimatize, a process which takes 1D6 weeks. Characters with the Zero-G skill at level 0 or better acclimatize instantly.

High-gravity worlds have a gravity 1.25 times or more than of Earth. They tend to be extremely dense worlds; common features include wide rocky plains, squat, muscular creatures, and plant life that spreads out like lichen instead of growing up. Crawling, burrowing or swimming are the commonest forms of locomotion. Humans find high-gravity worlds unpleasant. Especially high-gravity worlds require the use of pressured or powered suits to support the human frame. Characters on high-gravity worlds suffer a –1 DM to all skill checks until they acclimatize, a process which takes 1D6 weeks.

## Atmosphere

A planet's Atmosphere is generated by rolling 2D6–7 and adding the planet's Size. If a world's Size equals 0, then the world's Atmosphere equals 0. The Atmosphere code should never be higher than 15(F).

| Digit | Atmosphere | Pressure | Survival Gear Required |
| --- | --- | --- | --- |
| 0 | None | 0.00 | Vacc Suit |
| 1 | Trace | 0.001 to 0.09 | Vacc Suit |
| 2 | Very Thin, Tainted | 0.1 to 0.42 | Respirator, Filter |
| 3 | Very Thin | 0.1 to 0.42 | Respirator |
| 4 | Thin, Tainted | 0.43 to 0.7 | Filter |
| 5 | Thin | 0.43 to 0.7 |  |
| 6 | Standard | 0.71–1.49 |  |
| 7 | Standard, Tainted | 0.71–1.49 | Filter |
| 8 | Dense | 1.5 to 2.49 |  |
| 9 | Dense, Tainted | 1.5 to 2.49 | Filter |
| 10 (A) | Exotic | Varies | Air Supply |
| 11 (B) | Corrosive | Varies | Vacc Suit |
| 12 (C) | Insidious | Varies | Vacc Suit |
| 13 (D) | Dense, High | 2.5+ |  |
| 14 (E) | Thin, Low | 0.5 or less |  |
| 15 (F) | Unusual | Varies | Varies |

### Atmosphere Types

**Tainted:** Tainted atmospheres contain some element that is harmful to humans, such as an unusually high proportion of carbon dioxide. A character who breathes a tainted atmosphere without a filter will suffer 1D6 damage every few minutes (or hours, depending on the level of taint).

**Exotic:** An exotic atmosphere is unbreathable by humans, but is not otherwise hazardous. A character needs an air supply to breath in an exotic atmosphere.

**Corrosive:** Corrosive atmospheres are highly dangerous. A character who breathes in a corrosive atmosphere will suffer 1D6 damage each round.

**Insidious:** An insidious atmosphere is like a corrosive one, but it is so corrosive that it attacks equipment as well. The chief danger in an insidious atmosphere is that the toxic gases will destroy the seals and filters on the character's protective gear. An insidious atmosphere worms its way past protection after 2D6 hours on average, although vigilant maintenance or advanced protective gear can prolong survival times.

**Dense, High (D):** These worlds have thick N2/O2 atmospheres, but their mean surface pressure is too high to support unprotected human life (high pressure nitrogen and oxygen are deadly to humans). However, pressure naturally decreases with increasing altitude, so if there are highlands at the right altitude the pressure may drop enough to support human life. Alternatively, there may not be any topography high enough for humans to inhabit, necessitating floating gravitic or dirigible habitats or sealed habitats on the surface.

**Thin, Low (E):** The opposite of the Dense, High atmosphere, these massive worlds have thin N2/O2 atmospheres that settle in the lowlands and depressions and are only breathable there – the pressure drops off so rapidly with altitude that the highest topographic points of the surface may be close to vacuum.

**Unusual (F):** An Unusual atmosphere is a catchall term for an atmosphere that behaves in a strange manner. Examples include ellipsoidal atmospheres, which are thin at the poles and dense at the equator; Panthalassic worlds composed of a rocky core surrounded by a water layer hundreds of kilometers thick; worlds wracked by storms so intense that that the local air pressure changes from dense to thin depending on the current weather; and other planets with unusual and hazardous atmospheric conditions.

## Hydrographics

Hydrographic percentage is obtained by rolling 2D6–7 and adding the world's Size, modified by the world's atmosphere or size as described in the Hydrographic DMs by Size and Atmosphere table.

| Condition | DM |
| --- | --- |
| Size 0 or 1 | Hydrographics must be 0 |
| Atmosphere 0, 1, A, B or C | –4 |
| Atmosphere E | –2 |

A world's Hydrographics value should never exceed 10 (A), nor may it be lower than 0.

| Digit | Hydrographic Percentage | Description |
| --- | --- | --- |
| 0 | 0%–5% | Desert world |
| 1 | 6%–15% | Dry world |
| 2 | 16%–25% | A few small seas. |
| 3 | 26%–35% | Small seas and oceans. |
| 4 | 36%–45% | Wet world |
| 5 | 46%–55% | Large oceans |
| 6 | 56%–65% |  |
| 7 | 66%–75% | Earth-like world |
| 8 | 76%–85% | Water world |
| 9 | 86%–95% | Only a few small islands and archipelagos. |
| 10 (A) | 96–100% | Almost entirely water. |

## World Population

A world's Population is generated by rolling 2D6–2, modified by the world's Size, Atmosphere and Hydrographics as described in the Population DMs table. A world's Population value should never exceed 10 (A). If a world has a population of 0, it is uninhabited and the world also has a Government, Law Level and Technology Level of 0.

### Population DMs

| Condition | DM |
| --- | --- |
| Size is 2 or less | \-1 |
| Atmosphere is A or greater | \-2 |
| Atmosphere is 6 | +3 |
| Atmosphere is 5 or 8 | +1 |
| Hydrographics is 0 and Atmosphere less than 3 | \-2 |

### World Population

| Digit | Population | Range | Comparison |
| --- | --- | --- | --- |
| 0 | None | 0 |  |
| 1 | Few | 10+ | A tiny farmstead or a single family |
| 2 | Hundreds | 100+ | A village |
| 3 | Thousands | 1,000+ |  |
| 4 | Tens of thousands | 10,000+ | Small town |
| 5 | Hundreds of thousands | 100,000+ | Average city |
| 6 | Millions | 1,000,000+ |  |
| 7 | Tens of millions | 10,000,000+ | Large city |
| 8 | Hundreds of millions | 100,000,000+ |  |
| 9 | Billions | 1,000,000,000+ | Present day Earth |
| 10 (A) | Tens of billions | 10,000,000,000+ |  |

### Population Modifier

Sometimes it is enough just to know that a world has hundreds of millions of people on it (Population 8). Other times, a Referee or player may want a more specific number. The Population Modifier is determined by rolling 2D6-2. If the Population is greater than 0, the minimum Population Modifier value is 1. If the Population code is 0, then the Population Modifier is also 0. The Population Modifier is multiplied by 10 raised to the power of the Population code to determine a more specific number of people living on the world. For example, if the Referee generates a Population Modifier of 4 for a world with a Population code of 8, then he knows that (4×10<sup>8</sup>, which is…) 400,000,000 people live on that world.

## Primary Starport

Many worlds have starports, their presence being essential to interstellar trade and commerce. To determine the world's primary starport, roll 2D6-7 and add the world's Population value. Compare the result to the Primary Starport table to determine the starport class for the world. Each starport class offers different levels of service. The Starport Class Services table provides more specific details.

| Roll | Starport Class |
| --- | --- |
| 2 | X |
| 3 | E |
| 4 | E |
| 5 | D |
| 6 | D |
| 7 | C |
| 8 | C |
| 9 | B |
| 10 | B |
| 11+ | A |

### Starport Class Services

| Class | Descriptor | Best Fuel | Annual Maint. | Shipyard Capacity | Possible Bases |
| --- | --- | --- | --- | --- | --- |
| A | Excellent | Refined | Yes | Can construct starships and non-starships | Naval, Scout |
| B | Good | Refined | Yes | Can construct non-starships | Naval, Scout |
| C | Routine | Unrefined | No | Can perform reasonable repairs | Scout |
| D | Poor | Unrefined | No | None | Scout |
| E | Frontier | None | No | None | None |
| X | None | None | No | None | None |

## World Government

The Government characteristic is determined by rolling 2D6–7 and adding the world's Population. If a world's Population equals 0, then the world's Government equals 0. The Government code should never be higher than 15(F), nor lower than 0.

| Type | Government |
| --- | --- |
| 0 | None |
| 1 | Company/Corporation |
| 2 | Participating Democracy |
| 3 | Self-Perpetuating Oligarchy |
| 4 | Representative Democracy |
| 5 | Feudal Technocracy |
| 6 | Captive Government |
| 7 | Balkanization |
| 8 | Civil Service Bureaucracy |
| 9 | Impersonal Bureaucracy |
| 10 (A) | Charismatic Dictator |
| 11 (B) | Non-Charismatic Leader |
| 12 (C) | Charismatic Oligarchy |
| 13 (D) | Religious Dictatorship |
| 14 (E) | Religious Autocracy |
| 15 (F) | Totalitarian Oligarchy |

## Law Level

Law level is determined by rolling 2D6–7 and adding the Government characteristic. If the world's Government is 0, then the world's Law Level is also 0. Law Level should never be less than 0.

| Digit | Descriptor | Not Allowed |
| --- | --- | --- |
| 0 | No Law | No restrictions; candidate for Amber Zone status |
| 1 | Low Law | Poison gas, explosives, undetectable weapons, weapons or mass destruction |
| 2 | Low Law | Portable energy weapons (except ship-mounted weapons) |
| 3 | Low Law | Heavy weapons |
| 4 | Medium Law | Light assault weapons and submachine guns |
| 5 | Medium Law | Personal concealable weapons |
| 6 | Medium Law | All firearms except shotguns and stunners; carrying weapons discouraged |
| 7 | High Law | Shotguns |
| 8 | High Law | All bladed weapons, stunners |
| 9 | High Law | Any weapons outside one's residence; candidate for Amber Zone status |
| 10(A)+ | Extreme Law | Any weapons allowed at all; candidate for Amber Zone status |

## Technology Level

The Technology Level (also called "tech level" or TL) of the world is determined by rolling 1D6 and adding DMs per the Technology Level DMs by UWP Values table. A world's Technology Level may not be below 0.

| Value | Starport | Size | Atmosphere | Hydrographics | Population | Government |
| --- | --- | --- | --- | --- | --- | --- |
| 0 |  | +2 | +1 | +1 |  | +1 |
| 1 |  | +2 | +1 |  | +1 |  |
| 2 |  | +1 | +1 |  | +1 |  |
| 3 |  | +1 | +1 |  | +1 |  |
| 4 |  | +1 |  |  | +1 |  |
| 5 |  |  |  |  | +1 | +1 |
| 6 |  |  |  |  |  |  |
| 7 |  |  |  |  |  | +2 |
| 8 |  |  |  |  |  |  |
| 9 |  |  |  | +1 | +1 |  |
| 10 (A) | +6 |  | +1 | +2 | +2 |  |
| 11 (B) | +4 |  | +1 |  | +3 |  |
| 12 (C) | +2 |  | +1 |  | +4 |  |
| 13 (D) |  |  | +1 |  |  | –2 |
| 14 (E) |  |  | +1 |  |  | –2 |
| 15 (F) |  |  | +1 |  |  |  |
| X | –4 |  |  |  |  |  |

Certain world conditions must meet a minimum Technology Level requirement. If the world possesses a lower technology level, then the Referee should increase the world's tech level to the required minimum.

| Conditions | Minimum TL |
| --- | --- |
| Hydrographics is 0 or 10(A), Population is at least 6 | 4 |
| Atmosphere is 4, 7 or 9 | 5 |
| Atmosphere is 3 or less, or 10(A)-12(C) | 7 |
| Atmosphere is 13(D) or 14(E), Hydrographics is 10(A) | 7 |

## Trade Codes

Trade codes are assigned based on a world's UWP values, as noted in the UWP Values for Trade Codes table.

| Classification | Code | Size | Atmos. | Hydro | Pop. | Gov. | Law | TL |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Agricultural | Ag |  | 4–9 | 4–8 | 5–7 |  |  |  |
| Asteroid | As | 0 | 0 | 0 |  |  |  |  |
| Barren | Ba |  |  |  | 0 | 0 | 0 |  |
| Desert | De |  | 2+ | 0 |  |  |  |  |
| Fluid Oceans | Fl |  | 10+ | 1+ |  |  |  |  |
| Garden | Ga |  | 5,6,8 | 4–9 | 4–8 |  |  |  |
| High Population | Hi |  |  |  | 9+ |  |  |  |
| High Technology | Ht |  |  |  |  |  |  | 12+ |
| Ice-Capped | Ic |  | 0–1 | 1+ |  |  |  |  |
| Industrial | In |  | 0–2,4,7,9 |  | 9+ |  |  |  |
| Low Population | Lo |  |  |  | 1–3 |  |  |  |
| Low Technology | Lt |  |  |  |  |  |  | 5– |
| Non-Agricultural | Na |  | 0–3 | 0–3 | 6+ |  |  |  |
| Non-Industrial | Ni |  |  |  | 4–6 |  |  |  |
| Poor | Po |  | 2–5 | 0–3 |  |  |  |  |
| Rich | Ri |  | 6,8 |  | 6–8 |  |  |  |
| Water World | Wa |  |  | 10 |  |  |  |  |
| Vacuum | Va |  | 0 |  |  |  |  |  |

## Planetoid Belt Presence

Planetoid belts exist in many systems, and are mined by belters for ice, ore and other interesting things. To determine the presence of planetoid belts in a given star system, throw 4+ on 2D6 for at least one planetoid belt to be present in the system. If planetoid belts are present, then the number of planetoid belts in the system is 1D6-3, minimum of 1. If the primary world of the system is Size 0, then there's at least one planetoid belt in the system automatically.

## Gas Giant Presence

A star system may have one or more gas giant planets. The presence of a gas giant allows starships equipped with fuel scoops to refuel by skimming; this eliminates fuel cost for the vessel and increases profit. It also allows refueling at systems that do not have starports. Refueling in this fashion requires 1D6 hours per 40 tons of fuel.

Gas giants are relatively common. For each system throw 5+ on 2D6 for at least one gas giant to be present in the system. If gas giants are present, then the number of gas giants in the system is 1D6-2, minimum of 1.

## Bases

Stellar systems may have bases for military forces, the navy, the scouts, or for other arms of interstellar government. Bases can help determine political boundaries within a given region of space. An interstellar government will place bases along its borders to guard against aggression from rival states, or to control local systems. The presence of multiple bases within a few parsecs might indicate a contested border, or a mighty stronghold. While other bases may exist, the two primary bases are the Naval Base and the Scout Base.

### Naval Base

A naval base is a supply depot, refueling station, repair yard or fortress of the Navy. Naval vessels can obtain refined fuel and supplies here. If a world possesses a Class-A or Class-B starport, throw 8+ on 2D6 to determine the presence of a naval base in the system.

### Scout Base

A scout base or outpost offers refined fuel and supplies to scout ships. If a world does not possess a Class-E or Class-X starport, throw 7+ on 2D6 to determine the presence of a scout base in the system. This roll suffers a DM -1 if the world has a Class-C starport, a DM -2 for a Class-B starport and a DM -3 for a Class-A starport.

### Pirate Base

A pirate base serves as a haven for interstellar pirates. If a world does not possess a Class-A starport or a naval base, throw 12+ on 2D6 to determine the presence of a pirate base in the system.

### Base Codes

The presence of one or more bases is designated on the hex map with a base code in the upper-left of the world hex. The Base Codes table identifies which note-worthy bases, if any, are present.

| Code | Description |
| --- | --- |
| A | Naval Base and Scout Base/Outpost |
| G | Scout Base/Outpost and Pirate Base |
| N | Naval Base |
| P | Pirate Base |
| S | Scout Base/Outpost |

## Travel Zones

Most worlds are assumed to be civilized, or at least amenable to adventurers and other visitors. Some, however, are caught in the throes of war, plagued by disease, or simply not ready for interstellar visitors. Such worlds are classified by travel zones to denote such status. In most cases, the Referee should indicate travel zones based on the information available. Two such zone types exist: amber and red.

### Amber Zone

An Amber world has been deemed dangerous, and travelers are warned to be on their guard. Amber worlds are often undergoing upheaval or revolution, or else are naturally hazardous environments. A world with an Atmosphere of 10+, a government of 0, 7 or 10, or a Law Level of 0 or 9+ should be considered for Amber status.

### Red Zone

Red worlds are interdicted and travel to them is forbidden. Interdictions are enforced by the Navy. Red zones can indicate that the world is too dangerous to allow visitors. The Referee assigns Red worlds at his discretion.

## Polities and World Allegiance

Worlds may be independent, or part of a larger polity that spans a system or more. Polities range from loose confederations of a few worlds with common trade or defense policies or cultural links, to vast star empires containing thousands of systems and trillions of citizens. Polity borders should be drawn on the map. Note that larger polities will usually have sub-domains, which should also be marked.

## Communications Routes and Trade Routes

Within the subsector, governments will have established communications and trade routes connecting some (but not all) worlds. Messages between businesses, governments and people generally follow these routes.

Communications routes should be carefully drawn so as to avoid making all parts of the subsector accessible; a subsector should have some areas as backwaters for exploration and adventure. Communications routes are drawn as single lines connecting hexes on the subsector grid.

Trade routes link worlds that have strong commercial ties. Consult the Trade Route Worlds table– if any pair of worlds matching the two columns lay within four parsecs of each other, and there is a Jump–1 or Jump–2 route between them, then mark a trade route connecting those two worlds.

| First End Point | Second End Point |
| --- | --- |
| Industrial or High Tech | Asteroid, Desert, Ice Capped, Non-Industrial |
| High Population or Rich | Agricultural, Garden, Water World |
