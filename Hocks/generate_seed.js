const crypto = require("crypto");

// 6桁8行の数字を入力
const numbers = [222974, 1813, 324410, 332788, 457550, 273507, 360682, 564856];

// 数字を結合して文字列に変換
const combinedNumbers = numbers.join("");
console.log("Combined Numbers:", combinedNumbers);

// SHA-512ハッシュを生成
const hash = crypto.createHash("sha512").update(combinedNumbers).digest("hex");
console.log("SHA-512 Hash:", hash);

// 最初の32バイト（64文字）を取得
const seedHex = hash.substring(0, 64);
console.log("Seed Hex:", seedHex);

// Base58エンコード用の文字セット
const ALPHABET = "rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz";

// Base58エンコード関数
function base58Encode(hex) {
  let num = BigInt("0x" + hex);
  let str = "";
  while (num > 0) {
    str = ALPHABET[Number(num % 58n)] + str;
    num = num / 58n;
  }
  return str;
}

// シードを生成
const seed = "s" + base58Encode(seedHex);

console.log("\n=== 生成されたシード ===");
console.log("Seed:", seed);
console.log("========================\n");
console.log("警告: このシードは安全に保管してください！");
