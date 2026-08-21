/**
 * Generates a Cepheus Engine space encounter.
 *
 * @param {string} type - The kind of encounter to generate. Will randomly pick a type if not supplied.
 * @returns {string} The generated encounter HTML string.
 */
function spaceEncounterGenerator(type) {
    const ENCOUNTERS = {
        "alien": () => {
            const RESULT = ["Alien courier", "Alien frontier trader", "Alien merchant freighter", "Alien military vessel", "Alien raider", "Alien research vessel"];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "astrogation": () => {
            const RESULT = ["Asteroid (inhabited)", "Asteroid (uninhabited)", "Comet", "Interplanetary dust cloud", "Micrometeorite storm", "Solar flares"];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "hostileVessel": () => {
            const RESULT = [
                "Captured&nbsp;" + ENCOUNTERS["merchantVessel"](),
                "Captured&nbsp;" + ENCOUNTERS["militaryVessel"](),
                "Enemy&nbsp;" + ENCOUNTERS["militaryVessel"](),
                "<a href=\"../book2/common-vessels.html#tl9-raider\">Raider</a>",
                "Ship in distress (false)",
                "Ship in distress (true)"
            ];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "merchantVessel": () => {
            const RESULT = [
                "<a href=\"../book2/common-vessels.html#tl9-frontier-trader\">Frontier trader</a>",
                "<a href=\"../book2/common-vessels.html#tl9-frontier-trader\">Frontier trader</a>",
                "<a href=\"../book2/common-vessels.html#tl9-merchant-freighter\">Merchant freighter</a>",
                "<a href=\"../book2/common-vessels.html#tl9-merchant-liner\">Merchant liner</a>",
                "<a href=\"../book2/common-vessels.html#tl9-merchant-trader\">Merchant trader</a>",
                "<a href=\"../book2/common-vessels.html#tl9-merchant-trader\">Merchant trader</a>"
            ];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "militaryVessel": () => {
            const RESULT = [
                "<a href=\"../book2/common-vessels.html#tl11-corvette\">Corvette</a>",
                "<a href=\"../book2/common-vessels.html#tl11-destroyer\">Destroyer</a>",
                "<a href=\"../book2/common-vessels.html#tl11-patrol-frigate\">Patrol frigate</a>",
                "<a href=\"../book2/common-vessels.html#tl9-system-defense-boat\">System defense boat</a>",
                "<a href=\"../book2/common-vessels.html#tl9-system-monitor\">System monitor</a>",
                () => {
                    const RESULT = [
                        "<a href=\"../book2/common-vessels.html#tl14-dreadnought\">Dreadnought</a>",
                        "<a href=\"../book2/common-vessels.html#tl11-heavy-cruiser\">Heavy cruiser</a>",
                        "<a href=\"../book2/common-vessels.html#tl11-heavy-cruiser\">Heavy cruiser</a>",
                        "<a href=\"../book2/common-vessels.html#tl11-light-cruiser\">Light cruiser</a>",
                        "<a href=\"../book2/common-vessels.html#tl11-light-cruiser\">Light cruiser</a>",
                        "<a href=\"../book2/common-vessels.html#tl11-light-cruiser\">Light cruiser</a>"
                    ];
                    return RESULT[Math.floor(Math.random() * RESULT.length)];
                }];
            const RET = RESULT[Math.floor(Math.random() * RESULT.length)];
            return typeof RET === "function" ? RET() : RET;
        },
        "personalVessel": () => {
            const RESULT = [
                "<a href=\"../book2/common-vessels.html#tl9-asteroid-miner\">Asteroid miner</a>",
                "<a href=\"../book2/common-vessels.html#tl9-courier\">Courier</a>",
                "<a href=\"../book2/common-vessels.html#tl9-research-vessel\">Research vessel</a>",
                "<a href=\"../book2/common-vessels.html#tl11-survey-vessel\">Survey vessel</a>",
                "Unusual ship",
                "<a href=\"../book2/common-vessels.html#tl9-yacht\">Yacht</a>"
            ];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "spaceHabitat": () => {
            const RESULT = ["Medical facility", "Military facility", "Orbital factory", "Orbital habitat", "Refueling station or spaceport", "Research facility"];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "spaceJunk": () => {
            const RESULT = ["Astrogational buoy or beacon", "Communications satellite", "Debris from collision or attack", "Defense satellite", "Jettisoned cargo pod", "Lost or abandoned equipment or garbage"];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        },
        "spacecraft": () => {
            const RESULT = [
                "<a href=\"../book2/common-vessels.html#tl9-cutter\">Cutter</a>",
                "<a href=\"../book2/common-vessels.html#tl9-launch\">Launch or life boat</a>",
                "<a href=\"../book2/common-vessels.html#tl9-fighter\">Fighter</a>",
                "<a href=\"../book2/common-vessels.html#tl9-pinnace\">Pinnace</a>",
                "<a href=\"../book2/common-vessels.html#tl9-ships-boat\">Ship\'s boat</a>",
                "<a href=\"../book2/common-vessels.html#tl9-shuttle\">Shuttle</a>"
            ];
            return RESULT[Math.floor(Math.random() * RESULT.length)];
        }
    };
    if (!Object.keys(ENCOUNTERS).includes(type) || type === undefined) {
        return ENCOUNTERS[Object.keys(ENCOUNTERS)[Math.floor(Math.random() * Object.keys(ENCOUNTERS).length)]]();
    } else {
        return ENCOUNTERS[type]();
    }
}
