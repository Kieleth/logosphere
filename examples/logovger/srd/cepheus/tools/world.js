/**
 * Represents a Cepheus Engine world.
 * @typedef {Object} World
 * @property {string} [name="WorldName"] - The world's name. Defaults to "WorldName" if not supplied.
 * @property {string} [uwp] - The world's Universal World Profile (A123456-7). Automatically generated randomly if not supplied.
 * @property {string} [bases] - Naval/Scout/Pirate bases single-letter code. Automatically derived from UWP if not supplied.
 * @property {string} [remarks] - The world's Trade Codes. Automatically derived from UWP if not supplied.
 * @property {string} [travelZone] - Single-letter Amber or Red travel zone identifier. Automatically derived from UWP if not supplied.
 * @property {string} [pbg] - Population Modifier/Planetoid Belts/Gas Giants. Automatically derived from UWP if not supplied.
 * @property {string} [allegiance="Na"] - Two-letter allegiance code. Defaults to the non-aligned "Na" code if not supplied.
 * @property {string} [stellarData=""] - The world's stellar data. Defaults to an empty string if not supplied.
 */
class World {
    constructor(name, uwp, bases, remarks, travelZone, pbg, allegiance, stellarData) {
        this.name = name ? name : "WorldName";
        this.uwp = uwp ? uwp : generateUwp();
        this.bases = bases ? bases : generateBases(this.uwp);
        this.remarks = remarks ? remarks : generateTradeCodes(this.uwp);
        this.travelZone = travelZone ? travelZone : generateTravelZone(this.uwp);
        this.pbg = pbg ? pbg : generatePbg(this.uwp);
        this.allegiance = allegiance ? allegiance : "Na";
        this.stellarData = stellarData ? stellarData : "";
    }
    print() {
        return `${this.uwp} ${this.bases} ${this.remarks.padEnd(16, " ")} ${this.travelZone}  ${this.pbg} ${this.allegiance} ${this.stellarData}`;
    }
}

// Iterator that returns a World object with each next() until it exhausts its list of world names.
function* worldGenerator() {
    // Names from http://www.nameexoworlds.iau.org/, plus a few extras
    let names = [
        "Abol", "Absolutno", "Agouto", "Ahra", "Aiolos", "Alasia", "Albmi", "Alef", "Amadioha", "Amansinaya", "Anadolu", "Ananuca", "Aniara", "Arber", "Arcalis", "Astrolabos", "Asye", "Atakoraka", "Aumatex", "Awasis", "Awohali", "Axolotl", "Ayeyarwady", "Babylonia", "Baekdu", "Bagan", "Baiduri", "Bambaruush", "Banksia", "Barajeel", "Batsu", "Beirut", "Belel", "Belenos", "Belisama", "Bendida", "Berehynia", "Bibha", "Bocaprins", "Boinayel", "Bosona", "Bran", "Bubup", "Buna", "Buru", "Caleuche", "Catalineta", "Cayahuanca", "Ceibo", "Chaophraya", "Chason", "Chechia", "Chura", "Citadelle", "Citala", "Cocibolca", "Cruinlagh", "Cuancoa", "Cuptor", "Danfeng", "Decastro", "Dilmun", "Dingolay", "Ditso", "Diwo", "Diya", "Dofida", "Dombay", "Dopere", "Drukyul", "Ebla", "Eburonia", "Eiger", "Emiw", "Enaiposha", "Equiano", "Eyeke", "Felixvarela", "Filetdor", "Finlay", "Flegetonte", "Fold", "Formosa", "Franz", "Funi", "Gakyid", "Ganja", "Gar", "Ggantija", "Gloas", "Gnomon", "Gokturk", "Guahayona", "Guarani", "Guatauba", "Gumala", "Haik", "Hairu", "Halla", "Hamarik", "Hiisi", "Hoggar", "Horna", "Hunahpu", "Hunor", "Ibirapita", "Illyrian", "Independance", "Inquill", "Intan", "Iolaus", "Irena", "Isagel", "Isli", "Itonda", "Ixbalanque", "Iztok", "Jebus", "Kaewkosin", "Kalausi", "Kamuy", "Karaka", "Kaveh", "Kavian", "Kereru", "Khomsa", "Koeia", "Koit", "Komondor", "Kosjenka", "Koyopa", "Kralomoc", "Krotoa", "Kua'kua", "Laligurans", "Leklsullun", "Lerna", "Lete", "Levantes", "Liesma", "Lionrock", "Lucilinburhuc", "Lusitania", "Macondo", "Madalitso", "Madriu", "Maeping", "Mago", "Magor", "Mahsati", "Makombe", "Makropulos", "Malmok", "Marohu", "Maru", "Mastika", "Matza", "Mazalaai", "Melquiades", "Mintome", "Moldoveanu", "Monch", "Montuno", "Morava", "Moriah", "Mouhoun", "Mpingo", "Mulchatna", "Muspelheim", "Nachtwacht", "Najsakopajk", "Nakanbe", "Naledi", "Naqaya", "Naron", "Nasti", "Natasha", "Negoiu", "Nenque", "Neri", "Nervia", "Nikawiy", "Noifasui", "Noquisi", "Nosaxa", "Nushagak", "Nyamien", "Onasilos", "Orkaria", "Parumleo", "Peitruss", "Perwana", "Petra", "Phailinsiam", "Phoenicia", "Pincoya", "Pipitea", "Pipoltr", "Pirx", "Poerava", "Pollera", "Puli", "Qingluan", "Ramajay", "Rapeto", "Regoc", "Riosar", "Rosalia", "Sagarmatha", "Samagiya", "Samaya", "Sansuna", "Santamasa", "Sazum", "Shama", "Sharjah", "Sika", "Sissi", "Solaris", "Staburags", "Sterrennacht", "Stribor", "Su", "Sumajmajta", "Surt", "Tahay", "Taika", "Tangra", "Tanzanite", "Tapecue", "Tassili", "Teberda", "Tevel", "Timir", "Tislit", "Toge", "Tojil", "Tondra", "Trimobe", "Tryzub", "Tuiren", "Tumearandu", "Tupa", "Tupi", "Tylos", "Ugarit", "Uklun", "Umbaassa", "Uruk", "Uuba", "Veles", "Victoriapeak", "Viculus", "Viriato", "Vlasina", "Vytis", "Wadirum", "Wangshu", "Wattle", "Wouri", "Xihe", "Xolotl", "Xolotlan", "Yanyan", "Yvaga", "Zembra", "Zembretta",
        "Erehwemos", "Lacipyt", "Victoria", "Albert", "Diavlo", "Grizel", "Indeep", "Pynchan", "Ranther", "Sainte Foy", "Sharmun", "Taldor", "Vendetierre" // these are "special" Traveller names :)
    ];
    try {
        let usedNames = [];
        const MARKOV = new Markov(); // using https://www.npmjs.com/package/js-markov/v/2.0.3
        MARKOV.addStates(names);
        MARKOV.train();
        while (true) {
            let newName = MARKOV.generateRandom(13);
            if (!usedNames.includes(newName)) {
                yield generateWorld(newName, true);
                usedNames.push(newName);
            }
        }
    } catch (error) {
        console.error(error);
        console.warn("Warning! Couldn't use Markov generator, after predefined worlds are exhausted, remainder will be named (Unnamed).");
        for (let i = names.length - 1; i > 0; i--) { // shuffle the names
            const J = Math.floor(Math.random() * (i + 1));
            [names[i], names[J]] = [names[J], names[i]];
        }
        for (const NAME of names) {
            yield generateWorld(NAME, true);
        }
        while (true)
            yield generateWorld("(Unnamed)", true); // if the Markov generator fails, return "(Unnamed)"
    }
}

// Generates a Cepheus Engine UWP per the SRD rules
function generateUwp() {
    // World Size
    const SIZE = roll() - 2;

    // Atmosphere
    let atmosphere = 0;
    if (SIZE != 0) {
        atmosphere = Math.max(0, Math.min(roll() - 7 + SIZE, 15));
    }

    // Hydrographics
    let hydrographics = 0;
    if (SIZE > 1) {
        hydrographics = roll() - 7 + SIZE;
        if (atmosphere <= 1 || (atmosphere >= 10 && atmosphere <= 12)) {
            hydrographics -= 4;
        } else if (atmosphere == 15) {
            hydrographics -= 2;
        }
        hydrographics = Math.max(0, Math.min(hydrographics, 10));
    }

    // World Population
    let population = roll() - 2;
    if (SIZE <= 2)
        --population;
    if (atmosphere >= 10)
        population -= 2;
    else if (atmosphere == 6)
        population += 3;
    else if (atmosphere == 5 || atmosphere == 8)
        ++population;
    if (hydrographics == 0 && atmosphere < 3)
        population -= 2;
    population = Math.max(0, Math.min(population, 10));

    // Primary Starport
    let starport = roll() - 7 + population;
    if (starport <= 2)
        starport = "X";
    else if (starport >= 11)
        starport = "A";
    else {
        switch (starport) {
            case 3:
            case 4:
                starport = "E";
                break;
            case 5:
            case 6:
                starport = "D";
                break;
            case 7:
            case 8:
                starport = "C";
                break;
            case 9:
            case 10:
                starport = "B";
                break;
        }
    }

    // World Government
    let government = 0;
    if (population != 0)
        government = Math.max(0, Math.min(roll() - 7 + population, 15));

    // Law Level
    let lawLevel = 0;
    if (government != 0)
        lawLevel = Math.max(0, Math.min(roll() - 7 + government, 15));

    // Technology Level
    let technologyLevel = roll(1);
    switch (starport) {
        case "A":
            technologyLevel += 6;
            break;
        case "B":
            technologyLevel += 4;
            break;
        case "C":
            technologyLevel += 2;
            break;
        case "X":
            technologyLevel -= 4;
            break;
    }
    switch (SIZE) {
        case 0:
        case 1:
            technologyLevel += 2;
            break;
        case 2:
        case 3:
        case 4:
            ++technologyLevel;
            break;
    }
    switch (atmosphere) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            ++technologyLevel;
            break;
    }
    switch (hydrographics) {
        case 0:
        case 9:
            ++technologyLevel;
            break;
        case 10:
            technologyLevel += 2;
            break;
    }
    switch (population) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 9:
            ++technologyLevel;
            break;
        case 10:
            technologyLevel += 2;
            break;
        case 11:
            technologyLevel += 3;
            break;
        case 12:
            technologyLevel += 4;
            break;
    }
    switch (government) {
        case 0:
        case 5:
            ++technologyLevel;
            break;
        case 7:
            technologyLevel += 2;
            break;
        case 13:
        case 14:
            technologyLevel -= 2;
            break;
    }
    if ((hydrographics == 0 || hydrographics == 10) && population >= 6)
        technologyLevel = Math.max(4, technologyLevel);
    if (atmosphere == 4 || atmosphere == 7 || atmosphere == 9)
        technologyLevel = Math.max(5, technologyLevel);
    if (atmosphere <= 3 || (atmosphere >= 10 && atmosphere <= 12))
        technologyLevel = Math.max(7, technologyLevel);
    if ((atmosphere == 13 || atmosphere == 14) && hydrographics == 10)
        technologyLevel = Math.max(7, technologyLevel);
    technologyLevel = Math.max(0, technologyLevel);

    return `${starport}${pseudoHex(SIZE)}${pseudoHex(atmosphere)}${pseudoHex(hydrographics)}${pseudoHex(population)}${pseudoHex(government)}${pseudoHex(lawLevel)}-${pseudoHex(technologyLevel)}`;
}

// Generates Naval/Scout/Pirate bases for a supplied UWP string
function generateBases(uwp) {
    const STARPORT = uwp[0];
    let navalBase = false;
    let scoutBase = false;
    let pirateBase = false;
    let bases = " ";
    switch (STARPORT) {
        case "A":
            navalBase = roll() >= 8 ? true : false;
            scoutBase = roll() - 3 >= 7 ? true : false;
            break;
        case "B":
            navalBase = roll() >= 8 ? true : false;
            scoutBase = roll() - 2 >= 7 ? true : false;
            break;
        case "C":
            scoutBase = roll() - 1 >= 7 ? true : false;
            break;
        case "D":
            scoutBase = roll() >= 7 ? true : false;
            break;
    }
    if (!navalBase && STARPORT != "A")
        pirateBase = roll() >= 12 ? true : false;
    if (navalBase && scoutBase)
        bases = "A";
    else if (scoutBase && pirateBase)
        bases = "G";
    else if (navalBase)
        bases = "N";
    else if (pirateBase)
        bases = "P";
    else if (scoutBase)
        bases = "S";

    return bases;
}

// Generates Trade Codes for a supplied UWP string
function generateTradeCodes(uwp) {
    // let starport = pseudoHex(uwp[0]);
    const SIZE = pseudoHex(uwp[1]);
    const ATMOSPHERE = pseudoHex(uwp[2]);
    const HYDROGRAPHICS = pseudoHex(uwp[3]);
    const POPULATION = pseudoHex(uwp[4]);
    const GOVERNMENT = pseudoHex(uwp[5]);
    const LAW_LEVEL = pseudoHex(uwp[6]);
    // uwp[7] is "-"
    const TECHNOLOGY_LEVEL = pseudoHex(uwp[8]);

    // Trade Codes
    let tradeCodes = [];
    if (ATMOSPHERE >= 4 && ATMOSPHERE <= 9 && HYDROGRAPHICS >= 4 && HYDROGRAPHICS <= 8 && POPULATION >= 5 && POPULATION <= 7)
        tradeCodes.push("Ag");
    if (SIZE == 0 && ATMOSPHERE == 0 && HYDROGRAPHICS == 0)
        tradeCodes.push("As");
    if (POPULATION == 0 && GOVERNMENT == 0 && LAW_LEVEL == 0)
        tradeCodes.push("Ba");
    if (ATMOSPHERE >= 2 && HYDROGRAPHICS == 0)
        tradeCodes.push("De");
    if (ATMOSPHERE >= 10 && HYDROGRAPHICS >= 1)
        tradeCodes.push("Fl");
    if ([5, 6, 8].includes(ATMOSPHERE) && HYDROGRAPHICS >= 4 && HYDROGRAPHICS <= 9 && POPULATION >= 4 && POPULATION <= 8)
        tradeCodes.push("Ga");
    if (POPULATION >= 9)
        tradeCodes.push("Hi");
    if (TECHNOLOGY_LEVEL >= 12)
        tradeCodes.push("Ht");
    if (ATMOSPHERE <= 1 && HYDROGRAPHICS >= 1)
        tradeCodes.push("Ic");
    if ([0, 1, 2, 4, 7, 9].includes(ATMOSPHERE) && POPULATION >= 9)
        tradeCodes.push("In");
    if (POPULATION >= 1 && POPULATION <= 3)
        tradeCodes.push("Lo");
    if (TECHNOLOGY_LEVEL <= 5)
        tradeCodes.push("Lt");
    if (ATMOSPHERE <= 3 && HYDROGRAPHICS <= 3 && POPULATION >= 6)
        tradeCodes.push("Na");
    if (POPULATION >= 4 && POPULATION <= 6)
        tradeCodes.push("Ni");
    if (ATMOSPHERE >= 2 && ATMOSPHERE <= 5 && HYDROGRAPHICS <= 3)
        tradeCodes.push("Po");
    if ([6, 8].includes(ATMOSPHERE) && POPULATION >= 6 && POPULATION <= 8)
        tradeCodes.push("Ri");
    if (HYDROGRAPHICS == 10)
        tradeCodes.push("Wa");
    if (ATMOSPHERE == 0)
        tradeCodes.push("Va");

    return tradeCodes.join(" ");
}

// Generates Population Modifier/Planetoid Belts/Gas Giants for a supplied UWP string
function generatePbg(uwp) {
    const SIZE = pseudoHex(uwp[1]);
    const POPULATION = pseudoHex(uwp[4]);

    // Population Modifier
    let populationModifier = 0;
    if (POPULATION > 0)
        populationModifier = Math.max(1, Math.min(roll() - 3)); // Cepheus Engine is actually -2, however all other Traveller versions are at maximum 9; this is also needed for usage with Traveller Map

    // Planetoid Belt Presence
    let planetoidBelts = roll() >= 4 ? Math.max(1, roll(1) - 3) : 0;
    if (SIZE == 0)
        planetoidBelts = Math.max(1, planetoidBelts);

    // Gas Giant Presence
    const GAS_GIANTS = roll() >= 5 ? Math.max(1, roll(1) - 2) : 0;

    return `${pseudoHex(populationModifier)}${pseudoHex(planetoidBelts)}${pseudoHex(GAS_GIANTS)}`;
}

// Generates Amber travel zones for a supplied UWP string
function generateTravelZone(uwp) {
    const ATMOSPHERE = pseudoHex(uwp[2]);
    const GOVERNMENT = pseudoHex(uwp[5]);
    const LAW_LEVEL = pseudoHex(uwp[6]);
    let travelZone = " ";
    if (ATMOSPHERE >= 10 || GOVERNMENT == 0 || GOVERNMENT == 7 || GOVERNMENT == 10 || LAW_LEVEL == 0 || LAW_LEVEL >= 9)
        travelZone = "A";

    return travelZone;
}

/**
 * Creates a new World object with the specified name, handling special predefined world names.
 *
 * @param {string} [name] - The name to use for the world.
 * @param {boolean} outputAsObject - Whether to return a World object, or directly print the results.
 * @returns {World|string} The generated World object, or alternatively a string representation of it.
 */
function generateWorld(name, outputAsObject = false) {
    const WORLDS = {
        "Victoria": new World("Victoria", "X697770-4", " ", undefined, "R", "112", undefined, "K6 V"),
        "Sharmun": new World("Sharmun", "X86787A–5", undefined, undefined, "R"),
        "Taldor": new World("Taldor", "C866413-8"),
        "Ranther": new World("Ranther", "D539598-5"),
        "Pynchan": new World("Pynchan", "C656795-9", undefined, undefined, "A"),
        "Sainte Foy": new World("Sainte Foy", "B756733–7", undefined, undefined, "A"),
        "Grizel": new World("Grizel", "C768400-6", undefined, undefined, "A"),
        "Vendetierre": new World("Vendetierre", "C759685-8")
    }; // :)
    if ((name == "Victoria" && roll() == 12) || WORLDS[name])
        w = WORLDS[name];
    else
        w = new World(name);

    if (!outputAsObject)
        return w.print();
    else
        return w;
}
