const xrpl = require("xrpl");
require("dotenv").config();

// 共通設定
const XAHAU_TESTNET_URL = "wss://xahau-test.net/";
const ISSUER_ADDRESS = "rhyYNdxAyFQ7s2KYXhaTMJKF7NrkkZj1X9";
const CURRENCY_CODE = "JPY";

// クライアントの初期化
async function initClient() {
  const client = new xrpl.Client(XAHAU_TESTNET_URL);
  await client.connect();
  return client;
}

module.exports = {
  xrpl,
  XAHAU_TESTNET_URL,
  ISSUER_ADDRESS,
  CURRENCY_CODE,
  initClient,
};
