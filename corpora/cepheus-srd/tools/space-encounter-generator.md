# Space Encounter Generator

This tool will generate a Cepheus Engine Starship Encounter according to the rules found in [Chapter 15: Starship Encounters](../book3/starship-encounters.md).

<div>
    <center>
        <b>Encounter type:</b>
        <input type="radio" id="randomEncounter" name="encounterType" value="random" checked onclick="init()">
        <label for="randomEncounter">Random</label>
        <br>
        <input type="radio" id="alienVessel" name="encounterType" value="alien" onclick="init()">
        <label for="alienVessel">Alien Vessel</label>
        <input type="radio" id="derelict" name="encounterType" value="derelict" onclick="init()">
        <label for="derelict">Derelict</label>
        <input type="radio" id="spaceHabitat" name="encounterType" value="spaceHabitat" onclick="init()">
        <label for="spaceHabitat">Space Habitat</label>
        <input type="radio" id="astrogation" name="encounterType" value="astrogation" onclick="init()">
        <label for="astrogation">Astrogation</label>
        <input type="radio" id="spaceJunk" name="encounterType" value="spaceJunk" onclick="init()">
        <label for="spaceJunk">Space Junk</label>
        <br>
        <input type="radio" id="merchantVessel" name="encounterType" value="merchantVessel" onclick="init()">
        <label for="merchantVessel">Merchant Vessel</label>
        <input type="radio" id="personalVessel" name="encounterType" value="personalVessel" onclick="init()">
        <label for="personalVessel">Personal Vessel</label>
        <input type="radio" id="hostileVessel" name="encounterType" value="hostileVessel" onclick="init()">
        <label for="hostileVessel">Hostile Vessel</label>
        <input type="radio" id="militaryVessel" name="encounterType" value="militaryVessel" onclick="init()">
        <label for="militaryVessel">Military Vessel</label>
        <input type="radio" id="spacecraft" name="encounterType" value="spacecraft" onclick="init()">
        <label for="spacecraft">Spacecraft</label>
    </center>
</div>

<blockquote id="output" style="display:flex; justify-content: center; font-size: 2em"></blockquote>

<script src="space-encounter.js"></script>
<script>
    function init() {
        document.getElementById("output").innerHTML = spaceEncounterGenerator(document.querySelector('input[name="encounterType"]:checked').value);
    }
    init();
</script>
