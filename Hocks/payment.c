/**
 * XApay Custom Hook - Hackathon Version
 */

#include "hookapi.h"
#include <stdint.h>

// =====================================================================================================================
// == CONFIGURATION - ここをあなたの環境に合わせて設定してください ==
// =====================================================================================================================

// TODO: ここにXahau Testnet Faucetで取得したアカウントのアドレスを設定してください
unsigned char ISSUER_ACCID[20] = {
    0x5E, 0x32, 0xD1, 0x83, 0xA4, 0x33, 0x8D, 0x23,
    0x21, 0xC2, 0x62, 0xE2, 0x5A, 0x0B, 0x4B, 0x8A,
    0x9B, 0x85, 0xA3, 0xA2
};

// TODO: JPYステーブルコイン(SC)の通貨コード (160ビット)
//       (例: "SC " -> 0x534320...)
unsigned char CURRENCY_JPY[20] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4A, 0x50, 0x59, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// TODO: XApay運営サーバーのアカウントの16進数アドレス (20バイト)
//       (決済トリガートランザクションを送信するアカウント)
unsigned char OPERATOR_ACCID[20] = {
    0xBA, 0x55, 0x2D, 0x18, 0x3A, 0x43, 0x38, 0xD2,
    0x32, 0x1C, 0x26, 0x2E, 0x25, 0xA0, 0xB4, 0xB8,
    0xA9, 0xB8, 0x5A, 0x3A
};

// =====================================================================================================================

// Stateキーのプレフィックス
#define PREFIX_USER_BALANCE 0x55 // 'U'
#define PREFIX_NONCE        0x4E // 'N'

// 関数のプロトタイプ宣言
int64_t handle_charge();
int64_t handle_payment();


// --- メイン関数 ---
int64_t hook(uint32_t reserved)
{
    TRACESTR("XApay Hook: BGN");

    // フックを起動したトランザクションのタイプがPaymentか確認
    if (otxn_type() != ttPAYMENT) {
        accept(SBUF("XApay: Accepting non-payment transaction."), 0);
    }

    // Memoの有無で処理を分岐
    if (otxn_memo_count() > 0) {
        return handle_payment(); // 決済処理へ
    } else {
        return handle_charge(); // チャージ処理へ
    }
}

/**
 * @brief チャージ処理: ユーザーからの直接入金を処理する
 * @return 承認または拒否コード
 */
int64_t handle_charge()
{
    TRACESTR("XApay Hook: Handling Charge.");

    // 1. トランザクションのAmountフィールド(sfAmount)をシリアライズされたまま取得
    uint8_t amount_buffer[48]; // IOU Amountの最大サイズ
    int64_t amount_len = otxn_field(SBUF(amount_buffer), sfAmount);
    if (amount_len < 0) {
        rollback(SBUF("XApay Error(Charge): Could not get Amount field."), 10);
    }

    // 2. 通貨と発行者を厳密にチェック
    // AmountオブジェクトからsfIssuerフィールドを抽出
    uint8_t issuer_buffer[20];
    if (sto_subfield(amount_buffer, amount_len, SBUF(issuer_buffer), sfIssuer) < 0) {
        rollback(SBUF("XApay Error(Charge): Could not get Issuer from Amount."), 11);
    }

    // AmountオブジェクトからsfCurrencyフィールドを抽出
    uint8_t currency_buffer[20];
    if (sto_subfield(amount_buffer, amount_len, SBUF(currency_buffer), sfCurrency) < 0) {
        rollback(SBUF("XApay Error(Charge): Could not get Currency from Amount."), 12);
    }

    // 定義した発行者・通貨と一致するか検証
    if (!BUFFER_EQUAL(issuer_buffer, ISSUER_ACCID, 20) || !BUFFER_EQUAL(currency_buffer, CURRENCY_JPY, 20)) {
        rollback(SBUF("XApay Error(Charge): Invalid currency or issuer."), 13);
    }
    TRACESTR("XApay Hook: Currency and Issuer verified.");

    // 3. IOUの金額を正確に抽出
    int64_t amount_val; // IOUの整数値
    if (sto_amount_to_int64(&amount_val, amount_buffer, amount_len) < 0) {
        rollback(SBUF("XApay Error(Charge): Could not parse amount value."), 14);
    }
    if (amount_val <= 0) {
        rollback(SBUF("XApay Error(Charge): Amount must be positive."), 15);
    }

    // 4. Stateを更新
    uint8_t source_accid[20];
    otxn_source_account(SBUF(source_accid));

    uint8_t user_balance_key[21];
    user_balance_key[0] = PREFIX_USER_BALANCE;
    COPY(user_balance_key + 1, source_accid, 20);

    int64_t current_balance = 0;
    state_get(&current_balance, sizeof(current_balance), SBUF(user_balance_key));
    
    int64_t new_balance = current_balance + amount_val;
    state_set(&new_balance, sizeof(new_balance), SBUF(user_balance_key));

    accept(SBUF("XApay: Charge accepted successfully."), 0);
    return 0;
}

/**
 * @brief 決済処理: 運営サーバーからのトリガーで決済を実行する
 * @return 承認または拒否コード
 */
int64_t handle_payment()
{
    TRACESTR("XApay Hook: Handling Payment.");

    // 1. 送信元が運営アカウントであることを検証
    uint8_t source_accid[20];
    otxn_source_account(SBUF(source_accid));
    if (!BUFFER_EQUAL(source_accid, OPERATOR_ACCID, 20)) {
        rollback(SBUF("XApay Error(Payment): Unauthorized trigger."), 30);
    }
    TRACESTR("XApay Hook: Operator verified.");

    // 2. Memoを解析
    uint8_t user_accid[20];     
    int64_t amount;              
    uint8_t nonce[16];            
    uint8_t signature[72];        
    int64_t signature_len;        

    // 3. 署名検証
    uint8_t pubkey[33];
    if (util_accid(SBUF(pubkey), SBUF(user_accid)) < 0)
        rollback(SBUF("XApay Error(Payment): Could not get public key."), 31);

    uint8_t signed_data[16]; // 署名対象はNonce
    COPY(signed_data, nonce, 16);

    if (util_verify(SBUF(signed_data), SBUF(signature), SBUF(pubkey)) != 1)
        rollback(SBUF("XApay Error(Payment): Signature verification failed."), 32);

    TRACESTR("XApay Hook: Signature verified.");
    
    // 4. Nonce検証
    uint8_t nonce_key[17];
    nonce_key[0] = PREFIX_NONCE;
    COPY(nonce_key + 1, nonce, 16);
    if (state_get(0, 0, SBUF(nonce_key)) >= 0)
        rollback(SBUF("XApay Error(Payment): Nonce already used."), 33);

    TRACESTR("XApay Hook: Nonce is new.");
    
    // 5. 残高検証
    uint8_t user_balance_key[21];
    user_balance_key[0] = PREFIX_USER_BALANCE;
    COPY(user_balance_key + 1, user_accid, 20);

    int64_t user_balance = 0;
    state_get(&user_balance, sizeof(user_balance), SBUF(user_balance_key));
    if (user_balance < amount)
        rollback(SBUF("XApay Error(Payment): Insufficient balance."), 34);

    TRACESTR("XApay Hook: Balance is sufficient.");
    
    // 6. Stateの更新
    int64_t new_balance = user_balance - amount;
    state_set(&new_balance, sizeof(new_balance), SBUF(user_balance_key));

    uint8_t nonce_val = 1;
    state_set(&nonce_val, sizeof(nonce_val), SBUF(nonce_key));

    accept(SBUF("XApay: Payment processed successfully."), 0);
    return 0;
}