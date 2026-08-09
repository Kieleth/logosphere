// convert to and from pseudoHex automatically based on if parameter is a string or number
function pseudoHex(value) {
    const HEX = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"];
    if (typeof value === "number") {
        return HEX[value];
    } else if (typeof value === "string") {
        return HEX.indexOf(value);
    } else {
        console.error("Error! Can only convert number and string types to/from pseudohex.");
    }
}
