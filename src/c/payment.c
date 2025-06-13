/**
 * @file payment.c
 * @brief Xahauフック実装：JPYステーブルコインのチャージ＆ペイメントシステム
 *
 * 概要：
 * このフックは、特定アカウントにインストールされ、以下の2つの主要な機能を提供します。
 * 1. チャージ機能 (handle_charge):
 * ユーザーがこのアカウントに指定されたJPYトークンを送信すると、その残高をフックの内部ステートに記録します。
 * これは、ユーザーがサービスに資金をデポジットする（チャージする）操作に相当します。
 * 2. 支払い機能 (handle_allowance_payment):
 * ユーザーが事前に署名した「利用許可」に基づき、運営者が代理で決済を実行します。
 * これにより、ユーザーは決済のたびに署名する必要がなくなり、高速な少額決済が実現します。
 * フックは、利用許可署名の正当性と、許可された上限額を超えていないかを厳密に検証します。
 *
 * フックのトリガー：
 * - トランザクションタイプが `Payment` の場合 -> `handle_charge` を実行
 * - トランザクションタイプが `Invoke` の場合 -> `handle_allowance_payment` を実行
 */

#include "hookapi.h" // Hook APIの基本的な関数やマクロ

// --- 設定値 ---
// これらは実際に使用するアカウントやトークンの情報に合わせて書き換えてください。

// JPYトークンの発行者アカウントID (20バイトの16進数)
#define ISSUER_ACCID "131A2E9B489613A55E285F2363EA2284D7212255"
// JPYトークンの通貨コード (標準形式)
#define CURRENCY_JPY "0000000000000000000000004A50590000000000"
// 支払いトランザクションをトリガーする運営者アカウントID (20バイトの16進数)
#define OPERATOR_ACCID "F8D5A07335639A41EABF8C2224424263A21935E4"


// --- エラーコード定義 ---
// rollback時にどの処理で失敗したかを特定しやすくするためのコードです。
#define SUCCESS 0

// 汎用エラー
#define ERROR_INVALID_TRANSACTION 100
#define ERROR_INVALID_MEMO 101
#define ERROR_INVALID_JSON 102
#define ERROR_MISSING_FIELD 103
#define ERROR_UNKNOWN_PAYMENT_TYPE 104

// チャージ関連エラー
#define ERROR_CHARGE_INVALID_CURRENCY 201
#define ERROR_CHARGE_INVALID_ISSUER 202

// 支払い(Allowance)関連エラー
#define ERROR_ALLOWANCE_INVALID_ADDRESS 301
#define ERROR_ALLOWANCE_VERIFICATION_FAILED 302
#define ERROR_ALLOWANCE_EXCEEDED 303
#define ERROR_INSUFFICIENT_BALANCE 304


// --- ヘルパー関数 ---

/**
 * @brief ユーザーの内部残高をフックステートから取得する
 * @param user_accid ユーザーのアカウントID (20バイト)
 * @return ユーザーの残高 (XFL形式)
 */
int64_t get_user_balance(uint8_t* user_accid)
{
    uint8_t balance_buf[8];
    if (state(balance_buf, sizeof(balance_buf), user_accid, 20) == DOESNT_EXIST) {
        return 0; // 残高が存在しない場合は0を返す
    }
    return float_sto_to_int64(balance_buf);
}

/**
 * @brief ユーザーの内部残高をフックステートに保存する
 * @param user_accid ユーザーのアカウントID (20バイト)
 * @param new_balance 新しい残高 (XFL形式)
 */
void update_user_balance(uint8_t* user_accid, int64_t new_balance)
{
    uint8_t balance_buf[8];
    float_sto_set(balance_buf, new_balance);
    state_set(balance_buf, sizeof(balance_buf), user_accid, 20);
}


// --- 主要ロジック関数 ---

/**
 * @brief ユーザーからのJPYトークン入金を処理し、内部残高を更新する
 */
int64_t handle_charge()
{
    // --- 1. 送信元アカウントを取得 ---
    uint8_t source_accid[20];
    otxn_field(source_accid, sizeof(source_accid), sfAccount);

    // --- 2. トランザクションのAmountフィールドを解析 ---
    // トランザクション全体をメモリ上の「スロット」にロード
    int64_t oslot = otxn_slot(0);
    // Amountフィールドをサブスロットにロード
    int64_t aslot = slot_subfield(oslot, sfAmount, 0);

    // Amountが無効な場合はエラー
    if (aslot < 0) {
        rollback_str("Charge failed: Amount field is invalid.", 1);
        return 0; // unreachable
    }

    // --- 3. 通貨と発行者を検証 ---
    // 通貨コードをチェック
    uint8_t currency[20];
    slot_subfield(aslot, sfCurrency, currency, sizeof(currency));
    if (memcmp(currency, CURRENCY_JPY, 20) != 0) {
        accept_str("Charge ignored: Currency is not JPY.", SUCCESS);
        return 0;
    }

    // 発行者をチェック
    uint8_t issuer[20];
    slot_subfield(aslot, sfIssuer, issuer, sizeof(issuer));
    if (memcmp(issuer, ISSUER_ACCID, 20) != 0) {
        accept_str("Charge ignored: Issuer is incorrect.", SUCCESS);
        return 0;
    }

    // --- 4. 残高を更新 ---
    // 送金額を取得 (XFL形式)
    int64_t charge_amount_xfl = slot_float(aslot);

    // 現在の残高を取得し、加算して更新
    int64_t current_balance = get_user_balance(source_accid);
    int64_t new_balance = float_sum(current_balance, charge_amount_xfl);
    update_user_balance(source_accid, new_balance);

    accept_str("Charge successful.", SUCCESS);
    return 0;
}


/**
 * @brief "利用許可(Allowance)"モデルに基づいた支払いを処理する
 * @param data_ptr メモのJSONデータへのポインタ
 * @param data_len メモのJSONデータの長さ
 */
int64_t handle_allowance_payment(uint8_t* data_ptr, int64_t data_len)
{
    // --- 1. JSONからフィールドを抽出 ---
    uint8_t user_raddr[35], user_accid[20], payment_amount_str[20];
    uint8_t allowance_amount_str[20], allowance_sig_hex[148], allowance_sig[74];
    
    // ユーザーアドレスを取得
    if (sto_from_json(user_raddr, sizeof(user_raddr), data_ptr, data_len, "user_address") <= 0) 
        rollback_str("Payment failed: 'user_address' missing.", ERROR_MISSING_FIELD);
    if (util_raddr(user_accid, sizeof(user_accid), user_raddr, sizeof(user_raddr)) != 20) 
        rollback_str("Payment failed: Invalid user r-address.", ERROR_ALLOWANCE_INVALID_ADDRESS);

    // 支払い額を取得
    int64_t payment_amount_str_len = sto_from_json(payment_amount_str, sizeof(payment_amount_str), data_ptr, data_len, "payment_amount");
    if (payment_amount_str_len <= 0) rollback_str("Payment failed: 'payment_amount' missing.", ERROR_MISSING_FIELD);
    int64_t payment_amount_xfl = sto_amount_to_int64(payment_amount_str, payment_amount_str_len);
    
    // 利用許可額を取得
    int64_t allowance_amount_str_len = sto_from_json_nested(allowance_amount_str, sizeof(allowance_amount_str), data_ptr, data_len, "allowance.amount");
    if (allowance_amount_str_len <= 0) rollback_str("Payment failed: 'allowance.amount' missing.", ERROR_MISSING_FIELD);
    int64_t allowance_amount_xfl = sto_amount_to_int64(allowance_amount_str, allowance_amount_str_len);

    // 利用許可署名を取得
    int64_t allowance_sig_hex_len = sto_from_json_nested(allowance_sig_hex, sizeof(allowance_sig_hex), data_ptr, data_len, "allowance.signature");
    if (allowance_sig_hex_len <= 0) rollback_str("Payment failed: 'allowance.signature' missing.", ERROR_MISSING_FIELD);
    int64_t allowance_sig_len = util_hex_to_byte(allowance_sig, sizeof(allowance_sig), allowance_sig_hex, allowance_sig_hex_len);

    // --- 2. 利用許可署名の検証 ---
    uint8_t operator_raddr[35];
    uint8_t defined_operator_accid[] = {HEX_TO_BYTES(OPERATOR_ACCID)};
    util_accid(operator_raddr, sizeof(operator_raddr), defined_operator_accid, 20);
    
    uint8_t message[256];
    uint8_t* ptr = message;
    COPY(ptr, user_raddr, 34); ptr += 34; // r-addressはnull終端ではない可能性があるため長さを指定
    ptr[0] = ':'; ptr++;
    COPY(ptr, operator_raddr, 34); ptr += 34;
    ptr[0] = ':'; ptr++;
    COPY(ptr, allowance_amount_str, allowance_amount_str_len);
    int64_t message_len = 34 + 1 + 34 + 1 + allowance_amount_str_len;

    uint8_t user_pubkey[33];
    // 公開鍵取得処理 (本番ではより堅牢なエラー処理を推奨)
    int64_t kl_ret = util_keylet(user_pubkey, sizeof(user_pubkey), KEYLET_ACCOUNT, user_accid, 20, 0,0,0,0);
    int64_t slot_no = slot_set(user_pubkey, kl_ret);
    int64_t pubkey_len = slot_subfield(slot_no, sfRegularKey, user_pubkey, sizeof(user_pubkey));
    if (pubkey_len <= 0) pubkey_len = slot_subfield(slot_no, sfAccount, user_pubkey, sizeof(user_pubkey));
    
    if (util_verify(message, message_len, allowance_sig, allowance_sig_len, user_pubkey, pubkey_len) != 1) {
        rollback_str("Payment failed: Signature verification failed.", ERROR_ALLOWANCE_VERIFICATION_FAILED);
    }
    
    // --- 3. 利用上限と残高のチェック ---
    uint8_t state_key[20 + 148];
    COPY(state_key, user_accid, 20);
    COPY(state_key + 20, allowance_sig_hex, allowance_sig_hex_len);
    
    int64_t spent_amount_xfl = 0;
    uint8_t spent_amount_buf[8];
    if (state(spent_amount_buf, sizeof(spent_amount_buf), state_key, 20 + allowance_sig_hex_len) == 8) {
        spent_amount_xfl = float_sto_to_int64(spent_amount_buf);
    }

    int64_t new_spent_amount_xfl = float_sum(spent_amount_xfl, payment_amount_xfl);

    if (float_compare(new_spent_amount_xfl, allowance_amount_xfl, COMPARE_GREATER) == 1) {
        rollback_str("Payment failed: Amount exceeds allowance.", ERROR_ALLOWANCE_EXCEEDED);
    }

    int64_t user_balance = get_user_balance(user_accid);
    if (float_compare(payment_amount_xfl, user_balance, COMPARE_GREATER) == 1) {
        rollback_str("Payment failed: Insufficient balance.", ERROR_INSUFFICIENT_BALANCE);
    }

    // --- 4. 残高とステートの更新 ---
    int64_t new_balance = float_sum(user_balance, float_negate(0, payment_amount_xfl));
    update_user_balance(user_accid, new_balance);

    uint8_t new_spent_amount_buf[8];
    float_sto_set(new_spent_amount_buf, new_spent_amount_xfl);
    state_set(new_spent_amount_buf, sizeof(new_spent_amount_buf), state_key, 20 + allowance_sig_hex_len);
    
    accept_str("Payment processed successfully.", SUCCESS);
    return 0;
}


/**
 * @brief フックのエントリーポイント。トランザクションを解析し、適切な処理に振り分ける。
 */
int64_t hook(uint32_t reserved)
{
    // --- 1. トランザクションタイプの確認 ---
    int64_t tx_type = otxn_type();

    // Paymentトランザクションの場合はチャージ処理へ
    if (tx_type == ttPAYMENT) {
        return handle_charge();
    }
    // Invokeトランザクションの場合は支払い処理へ
    else if (tx_type == ttINVOKE) {
        // --- 2. 運営者アカウントの検証 ---
        uint8_t operator_accid[20];
        otxn_field(operator_accid, sizeof(operator_accid), sfAccount);
        uint8_t defined_operator_accid[] = {HEX_TO_BYTES(OPERATOR_ACCID)};

        if (memcmp(operator_accid, defined_operator_accid, 20) != 0) {
            rollback_str("Invoke rejected: Not from authorized operator.", ERROR_INVALID_TRANSACTION);
        }

        // --- 3. メモの解析と支払いタイプによる分岐 ---
        unsigned char memo_buffer[1024];
        int64_t memo_len = otxn_param(memo_buffer, sizeof(memo_buffer), sfMemos);
        if (memo_len <= 0) rollback_str("Payment failed: Memo is missing.", ERROR_INVALID_MEMO);

        int64_t data_len = sto_subfield(memo_buffer, memo_len, sfMemoData);
        if (data_len <= 0) rollback_str("Payment failed: MemoData is missing.", ERROR_INVALID_JSON);

        uint8_t* data_ptr = SUB_OFFSET(memo_len) + memo_buffer;

        // "type"フィールドの値で処理を分岐
        uint8_t type_buf[32];
        int64_t type_len = sto_from_json(type_buf, sizeof(type_buf), data_ptr, data_len, "type");
        if (type_len <= 0) rollback_str("Payment failed: 'type' is missing in Memo.", ERROR_MISSING_FIELD);

        uint8_t allowance_type[] = "allowance_payment";
        if (type_len == 17 && memcmp(type_buf, allowance_type, 17) == 0) {
            return handle_allowance_payment(data_ptr, data_len);
        }

        rollback_str("Payment failed: Unknown payment type.", ERROR_UNKNOWN_PAYMENT_TYPE);
    }

    // 対応していないトランザクションタイプは無視して受諾
    accept_str("Transaction type not handled by this hook.", SUCCESS);
    return 0;
}