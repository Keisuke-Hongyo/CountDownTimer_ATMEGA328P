/*******************************************************************/
/*               Count Down Timer Program Ver 1.0                  */
/*          Platform      : Arduino Mini(ATMEGA328P 3.3V 8MHz)     */
/*          Program Lang  : Arduino Lang(Using C++)                */
/*          Date          : 05 DEC 2020                            */
/*          Program       : Keisuke Hongyo                         */
/*          Affiliation   : Kanonjisougo High School               */
/*                          Depertment of Electronic               */
/*          Collaborator  : Double-SH,Yukinko                      */
/*******************************************************************/

/* インクルードファイル */
#include <Arduino.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include <stdio.h>

/* 定義 */
#define DATAPIN 13  // 74HC595のDSへ
#define LATCHPIN 14 // 74HC595のST_CPへ
#define CLOCKPIN 15 // 74HC595のSH_CPへ
#define ONEPPS 2    // GPS 1PPS
#define LEDOUT 12   // LED

/* シフトレジスタ出力ビット */
#define MAXBIT 24

/* 7セグメントパターン*/
#define SEG_0 0xbd
#define SEG_1 0x11
#define SEG_2 0x7c
#define SEG_3 0x75
#define SEG_4 0xd1
#define SEG_5 0xe5
#define SEG_6 0xed
#define SEG_7 0xb1
#define SEG_8 0xfd
#define SEG_9 0xf5
#define BLKOUT 0x00
#define BAR 0x40

// タクトスイッチ割り当て
#define SW1 3
#define SW2 4
#define SW3 5

// タクトスイッチ番号
#define TACTSW_1 0
#define TACTSW_2 1
#define TACTSW_3 2

// 日付の最小値と最大値
#define MAXDAY 300
#define MINDAY 0
#define MAXDIGIT 999

// 制御定数
#define OFF 0x01
#define ON 0x00

/* ソフトウェアシリアルの設定 */
SoftwareSerial mySerial(17, 16); // RX, TX

/* GPSクラス*/
TinyGPSPlus gps;

/* LCDクラス */
LiquidCrystal lcd(11, 10, 9, 8, 7, 6);

/* セグメントデータ格納変数 */
const unsigned char seg[10] = {SEG_0, SEG_1, SEG_2, SEG_3, SEG_4,
                               SEG_5, SEG_6, SEG_7, SEG_8, SEG_9};

/* GPS関係 */
volatile unsigned char gpsState; // GPS受信状態

/* 出力データの構造体・共用体*/
typedef struct SegmentData
{
    unsigned long dig_1 : 8;   // 1の位　      下位ビット
    unsigned long dig_10 : 8;  // 10の位
    unsigned long dig_100 : 8; // 100の位　    上位ビット
};

typedef union Outdata
{
    SegmentData segData; // 各桁の構造体
    unsigned long outdata;
};

// 日本標準時
typedef struct jstDate
{
    unsigned int year;
    unsigned char month;
    unsigned char day;
    unsigned char hour;
    unsigned char minute;
    unsigned char second;
};

typedef struct setDate
{
    unsigned int yy;
    unsigned char mm;
    unsigned char dd;
};

/* 日付の格納　構造体*/
setDate setdate;

// 月の日数
const unsigned char mthDay[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// タクトスイッチ用構造体
typedef struct stateSW
{
    unsigned char isOn : 1;    // 0: SW OFF 1: SW ON
    unsigned char isState : 1; // 0:スイッチ変化なし 1: SW ON->OFF
};

typedef struct TactSw
{
    stateSW tactSw[3];
};

/* 最大日数格納変数 */
unsigned int MaxDay;

/* 残り日数保存変数 */
volatile long dd;


/*******************************************************************************
 *   ShiftRegOut - シフトレジスタへの出力関数                                     *
 *    bit    : 出力するビット数を指定                                             *
 *    val    : 出力するデータ                                                    *
 *    return : 戻り値なし　　　　　　　　　　　　　　　　　　　　　　　　            *
 *******************************************************************************/
void ShiftRegOut(unsigned char bit, unsigned long val)
{
    digitalWrite(LATCHPIN, LOW); // 送信中はLATCHPINをLOWに
    delay(5);
    // シフトレジスタにデータを送る
    for (int i = 0; i < bit; i++)
    {
        // データの設定
        digitalWrite(DATAPIN, !!(val & (1L << i)));

        //　書き込みクロック
        digitalWrite(CLOCKPIN, HIGH);
        delay(5);
        digitalWrite(CLOCKPIN, LOW);
        delay(5);
    }

    digitalWrite(LATCHPIN, HIGH); // 送信後はLATCHPINをHIGHに戻す
    delay(5);
}

/*******************************************************************************
 *    setDig - 桁のデータのセット関数                                             *
 *    n          : 値　　　　　　　　　　                                         *
 *    *segment   : セグメント出力用構造体のポインタ                                *
 *    return     : 戻り値なし　　　　　　　　　　　　　　　　　　　　　　            *
 *******************************************************************************/
void setDig(long n, Outdata *segment)
{
    unsigned int place_100, place_10, place_1;

    /* マイナスの時の処理*/
    if (n <= 0)
    {
        segment->segData.dig_100 = segment->segData.dig_10 = segment->segData.dig_1 = BAR;
    }
    /* それ以外の処理 */
    else
    {
        /* 最大値を超えたときの処理 */
        if (n >= MaxDay)
            n = MaxDay;

        /* 各桁の計算*/
        place_100 = n / 100;
        place_10 = (n % 100) / 10;
        place_1 = n % 10;

        if (place_100 == 0)
            segment->segData.dig_100 = BLKOUT;
        else
            segment->segData.dig_100 = seg[(place_100)];

        /* 10の位の処理*/
        if ((place_10 == 0) && (place_100 == 0))
            segment->segData.dig_10 = BLKOUT;
        else
            segment->segData.dig_10 = seg[(place_10)];

        /* 1の位の処理*/
        segment->segData.dig_1 = seg[place_1];
    }
}

// 日付処理
/*******************************************************************************
 *    convMjd - グレゴリオ暦を修正ユリウス日へ変換                                 *
 *    year           : 年（西暦)    　　                                         *
 *    month          : 月  　　 　　　　                                         *
 *    day            : 日  　　 　　　　                                         *
 *    return         : 残り日数 　　　　　　　　　　　　　　　　　　　　            *
 *******************************************************************************/
long convMjd(unsigned int year, unsigned char month, unsigned char day)
{
    if (month <= 2)
    {
        year--;
        month += 12;
    }

    int dy = 365 * (year - 1); // 経過年数×365日
    int c = year / 100;
    int dl = (year >> 2) - c + (c >> 2); // うるう年分
    int dm = (month * 979 - 1033) >> 5;  // 1月1日から m 月1日までの日数
    return dy + dl + dm + day - 1;
}

/*******************************************************************************
 *    utcTojst - 協定世界時(UTC)から日本標準時(JST)に変換                         *
 *    jstDate        : 日本標準時刻格納構造体                                    *
 *    return         : none                                    　　            *
 *******************************************************************************/
void utcTojst(jstDate *jst)
{
    unsigned char p;

    /* GPSデータを格納*/
    jst->year = gps.date.year();
    jst->month = gps.date.month();
    jst->day = gps.date.day();
    jst->hour = gps.time.hour();
    jst->minute = gps.time.minute();
    jst->second = gps.time.second();

    /* 世界標準時->日本標準時(時差は+9時間) */
    jst->hour = jst->hour + 9;

    /*日付時間を超えたとき*/
    if (jst->hour >= 24)
    {
        jst->hour = jst->hour - 24; // 時間の表示変更
        jst->day = jst->day + 1;    // 日付の変更

        /* 2月 -> 閏年を求める */
        if (jst->month == 2)
        {
            /* 閏年の確認 */
            if ((jst->year) % 400 == 0 || (jst->year) % 4 == 0 && (jst->year) % 100 != 0)
                p = 1;
            else
                p = 0;

            if ((mthDay[jst->month - 1] + p) < jst->day)
            {
                jst->month += 1;
                jst->day = 1;
            }
        }
        /* 2月以外 */
        else
        {
            if (mthDay[jst->month - 1] < jst->day)
            {
                jst->month += 1;
                jst->day = 1;
            }
        }
    }
}
/*******************************************************************************
 * printDebug - 動作確認用出力関数（シリアルで出力                                 *
 *    argument : jst : 日本標準時刻格納構造体  d:残り日数計算結果　　               *
 *    return   : None                                                           *
 *******************************************************************************/
void printDebug(jstDate jst, unsigned long d)
{
    Serial.print(jst.year);
    Serial.print("-");
    Serial.print(jst.month);
    Serial.print("-");
    Serial.print(jst.day);
    Serial.print(" ");
    Serial.print(jst.hour);
    Serial.print(":");
    Serial.print(jst.minute);
    Serial.print(":");
    Serial.println(jst.second);
    Serial.print("yy:");
    Serial.println(setdate.yy);
    Serial.print("mm:");
    Serial.println(setdate.mm);
    Serial.print("dd:");
    Serial.println(setdate.dd);
    Serial.print("d:");
    Serial.println(d);
}

/*******************************************************************************
 * checkSw - スイッチ状態確認                                                    *
 *    argument : swFlg -> スイッチ状態変数                                       *
 *               00   xx  xx  xx   (xx: 00 SW OFF 01 SW ON 11:SW ON -> SW OFF)  *
 *               None SW3 SW2 SW1                                               *
 *    return   : None                                                           *
 *******************************************************************************/
unsigned char checkSw(TactSw *swFlg)
{
    // スイッチが押されたか確認
    if (digitalRead(SW1) == ON)
    {
        delay(1);
        if (digitalRead(SW1) == ON)
            swFlg->tactSw[TACTSW_1].isOn = 0x01;
    }
    if (digitalRead(SW2) == ON)
    {
        delay(1);
        if (digitalRead(SW2) == ON)
            swFlg->tactSw[TACTSW_2].isOn = 0x01;
    }
    if (digitalRead(SW3) == ON)
    {
        delay(1);
        if (digitalRead(SW3) == ON)
            swFlg->tactSw[TACTSW_3].isOn = 0x01;
    }
    // スイッチが押されてからスイッチがOFFになったか
    if ((digitalRead(SW1) == OFF) && (swFlg->tactSw[TACTSW_1].isOn == 0x01))
    {
        swFlg->tactSw[TACTSW_1].isState = 0x01;
        swFlg->tactSw[TACTSW_1].isOn = 0x00;
    }

    if ((digitalRead(SW2) == OFF) && (swFlg->tactSw[TACTSW_2].isOn == 0x01))
    {
        swFlg->tactSw[TACTSW_2].isState = 0x01;
        swFlg->tactSw[TACTSW_2].isOn = 0x00;
    }

    if ((digitalRead(SW3) == OFF) && (swFlg->tactSw[TACTSW_3].isOn == 0x01))
    {
        swFlg->tactSw[TACTSW_3].isState = 0x01;
        swFlg->tactSw[TACTSW_3].isOn = 0x00;
    }
}
/*******************************************************************************
 * setDistDay - 日数設定関数                    　                                  *
 *    argument : sw -> スイッチ状態格納構造体                                     *
 *    return   : None                                                           *
 *******************************************************************************/
void setDistDay(TactSw *sw)
{
    unsigned char mode = 0;
    unsigned char mode_bak;
    bool procEnd = false;

    unsigned int yy;
    unsigned char mm;
    unsigned char dd;
    unsigned char p;

    /* */
    yy = setdate.yy;
    mm = setdate.mm;
    dd = setdate.dd;

    lcd.clear();

    do
    {
        checkSw(sw);

        if (sw->tactSw[TACTSW_1].isState == 0x01)
        {
            mode++;
            sw->tactSw[TACTSW_1].isState = 0x00;
        }

        if (mode != mode_bak)
            lcd.clear();

        switch (mode)
        {

        case 0: /* 年の変更 */
            if (sw->tactSw[TACTSW_2].isState == 0x01)
            {
                yy++;
                sw->tactSw[TACTSW_2].isState = 0x00;
            }
            if (sw->tactSw[TACTSW_3].isState == 0x01)
            {
                yy--;
                sw->tactSw[TACTSW_3].isState = 0x00;
            }
            /* 最小・最大の設定*/
            if (yy < 2000)
                yy = 2000;
            if (yy > 2100)
                yy = 2099;
            lcd.setCursor(0, 0);
            lcd.print("Set Dest day    ");
            lcd.setCursor(0, 1);
            lcd.print("Year=");
            lcd.print(yy);
            break;
        case 1: /* 月の変更 */
            if (sw->tactSw[TACTSW_2].isState == 0x01)
            {
                mm++;
                sw->tactSw[TACTSW_2].isState = 0x00;
            }
            if (sw->tactSw[TACTSW_3].isState == 0x01)
            {
                mm--;
                sw->tactSw[TACTSW_3].isState = 0x00;
            }

            /* 最小・最大の設定*/
            if (mm <= 0)
                mm = 12;
            if (mm > 12)
                mm = 1;

            lcd.setCursor(0, 0);
            lcd.print("Set Dest day    ");
            lcd.setCursor(0, 1);
            lcd.print("Month=");
            lcd.print(mm);
            lcd.print("   ");
            break;

        case 2: /* 日の変更 */
            if (sw->tactSw[TACTSW_2].isState == 0x01)
            {
                dd++;
                sw->tactSw[TACTSW_2].isState = 0x00;
            }
            if (sw->tactSw[TACTSW_3].isState == 0x01)
            {
                dd--;
                sw->tactSw[TACTSW_3].isState = 0x00;
            }
            /* 最小・最大の設定*/
            if (mm == 2)
            {
                /* うる年の検出 */
                if ((yy) % 400 == 0 || (yy) % 4 == 0 && (yy) % 100 != 0)
                    p = 1;
                else
                    p = 0;
            }
            else
                p = 0;

            if (dd <= 0)
                dd = mthDay[mm - 1] + p;
            if (dd > (mthDay[mm - 1] + p))
                dd = 1;

            /* 表示 */
            lcd.setCursor(0, 0);
            lcd.print("Set Dest day    ");
            lcd.setCursor(0, 1);
            lcd.print("Day=");
            lcd.print(dd);
            lcd.print("   ");
            break;

        /* 最大表示日数の設定 */
        case 3:
            /* ボタン操作 */
            if (sw->tactSw[TACTSW_2].isState == 0x01)
            {
                MaxDay++;
                sw->tactSw[TACTSW_2].isState = 0x00;
            }
            if (sw->tactSw[TACTSW_3].isState == 0x01)
            {
                MaxDay--;
                sw->tactSw[TACTSW_3].isState = 0x00;
            }

            /* 最大値と最小値の設定処理 */
            if (MaxDay <= 0)
                MaxDay = 0;
            if (MaxDay > MAXDIGIT)
                MaxDay = MAXDIGIT;

            /* 表示 */
            lcd.setCursor(0, 0);
            lcd.print("Display Maxday  ");
            lcd.setCursor(0, 1);
            lcd.print("Max Day=");
            lcd.print(MaxDay);
            lcd.print("   ");
            break;

        case 4:
            lcd.setCursor(0, 0);
            lcd.print(" Data Write? ");
            lcd.setCursor(0, 1);
            lcd.print(" SW2:Yes SW3:NO ");

            if (sw->tactSw[TACTSW_2].isState == 0x01)
            {
                /* EEPROM 書き込み */
                EEPROM.write(0x000, 0x01);
                EEPROM.write(0x001, yy / 100);
                EEPROM.write(0x002, yy % 100);
                EEPROM.write(0x003, mm);
                EEPROM.write(0x004, dd);
                EEPROM.write(0x005, MaxDay / 100);
                EEPROM.write(0x006, MaxDay % 100);

                /* データを設定 */
                setdate.yy = yy;
                setdate.mm = mm;
                setdate.dd = dd;

                procEnd = true;
                sw->tactSw[TACTSW_2].isState = 0x00;
            }

            if (sw->tactSw[TACTSW_3].isState == 0x01)
            {
                procEnd = true;
                sw->tactSw[TACTSW_3].isState = 0x00;
            }

            break;
        default:
            lcd.setCursor(0, 0);
            lcd.print(" Error ");
            break;
        }
        if (mode > 4)
            mode = 0;

        mode_bak = mode;
    } while (procEnd == false);
}

/* メッセージ表示 */
void msg(jstDate jst, long d)
{
    char buf[17];

    sprintf(buf, "Set:%4d/%02d/%02d", setdate.yy, setdate.mm, setdate.dd);
    lcd.setCursor(0, 0);
    lcd.print(buf);

    sprintf(buf, "Days Left:%3dday", d);
    lcd.setCursor(0, 1);
    lcd.print(buf);
}

/*******************************************************************************
 * CheckMode - 表示確認関数　　　　                                                    *
 *******************************************************************************/
void CheckMode(void)
{
    Outdata segment;
    unsigned char i;
    static long d = 0;

    lcd.setCursor(0, 0);
    lcd.print("Test Mode");
    lcd.setCursor(0, 1);
    lcd.print("Count up Segment");
    while (1)
    {
        for (i = 0; i <= 9; i++)
        {
            segment.segData.dig_1 = seg[i];
            segment.segData.dig_10 = seg[i];
            segment.segData.dig_100 = seg[i];
            ShiftRegOut(MAXBIT, segment.outdata);
            delay(500);
        }

        segment.segData.dig_1 = BAR;
        segment.segData.dig_10 = BAR;
        segment.segData.dig_100 = BAR;
        ShiftRegOut(MAXBIT, segment.outdata);
        delay(500);

        segment.segData.dig_1 = BLKOUT;
        segment.segData.dig_10 = BLKOUT;
        segment.segData.dig_100 = BLKOUT;
        ShiftRegOut(MAXBIT, segment.outdata);
        delay(500);

    }
}
/*******************************************************************************
 * setup - 初期化関数　　　　                                                    *
 *******************************************************************************/
void setup()
{
    // 入出力ピンの設定
    pinMode(DATAPIN, OUTPUT);
    pinMode(LATCHPIN, OUTPUT);
    pinMode(CLOCKPIN, OUTPUT);
    pinMode(LEDOUT, OUTPUT);
    pinMode(ONEPPS, INPUT_PULLUP);
    pinMode(SW1, INPUT_PULLUP);
    pinMode(SW2, INPUT_PULLUP);
    pinMode(SW3, INPUT_PULLUP);

    /*シリアル設定 */
    mySerial.begin(9600); // GPSデータ取得用
    Serial.begin(9600);   // デバッグ出力用

    /* EEPROM初期設定 */
    if (EEPROM.read(0x000) != 0x01)
    {
        /* 初期設定 */
        EEPROM.write(0x000, 0x01);
        EEPROM.write(0x001, 20);
        EEPROM.write(0x002, 20);
        EEPROM.write(0x003, 12);
        EEPROM.write(0x004, 31);
        EEPROM.write(0x005, 3);
        EEPROM.write(0x006, 0);
    }

    /* EEPROMデータ読み込み */
    setdate.yy = EEPROM.read(0x001) * 100 + EEPROM.read(0x002); // Year
    setdate.mm = EEPROM.read(0x003);                            // Month
    setdate.dd = EEPROM.read(0x004);                            // Day
    MaxDay = EEPROM.read(0x005) * 100 + EEPROM.read(0x006);

    /* LCD設定 */
    lcd.begin(16, 2);

    /* 初期7セグ表示 */
    ShiftRegOut(MAXBIT, 0x404040);

    /* GPS受信状態変数初期化 */
    gpsState = OFF;

    /* 残り日数初期化 */
    dd = MAXDIGIT + 1;
}

/* メインループ関数 */
void loop()
{
    unsigned int i;
    Outdata segment;
    static long d = 0;
    static jstDate jst;
    static TactSw sw;

    if (digitalRead(SW1) == ON)
    {
        CheckMode();
    }

    // メイン
    while (1)
    {
        // LCD表示
        msg(jst, d);

        // LED
        if (digitalRead(ONEPPS) == ON)
        {
            digitalWrite(LEDOUT, HIGH);
        }
        else
        {
            digitalWrite(LEDOUT, LOW);
        }

        /* スイッチ検出 */
        checkSw(&sw);

        if (sw.tactSw[TACTSW_1].isState == 0x01)
        {
            for (int i = TACTSW_1; i <= TACTSW_3; i++)
                sw.tactSw[i].isState = 0x00;

            // 日時設定処理
            digitalWrite(LEDOUT, LOW);
            setDistDay(&sw);

            // 残り日数を計算
            d = convMjd(setdate.yy, setdate.mm, setdate.dd) - convMjd(jst.year, jst.month, jst.day);
            if (d > MAXDIGIT)
                d = MAXDIGIT;

            // 7セグメント表示
            setDig(d, &segment);
            ShiftRegOut(MAXBIT, segment.outdata);
            dd = d;

            // 液晶をクリア
            lcd.clear();
        }

        // GPSデータ　読み込み
        while (mySerial.available() > 0)
        {

            // GPSデータ読み取り
            gps.encode(mySerial.read());

            if (gps.time.isUpdated())
            {
                // 日本時間に変換
                utcTojst(&jst);

                // 残り日数を計算
                d = convMjd(setdate.yy, setdate.mm, setdate.dd) - convMjd(jst.year, jst.month, jst.day);
                if (d > MAXDIGIT)
                    d = MAXDIGIT;

                // シリアルモニター用
                // Debug Console
                // printDebug(jst, d);

                // 残り日数に変化があれば表示を更新する。
                if (d != dd)
                {
                    setDig(d, &segment);
                    ShiftRegOut(MAXBIT, segment.outdata);
                    dd = d;
                }
            }
        }
    }
}