# カウントダウンタイマーの製作
## 1. はじめに
 これはArduinoを使用したカウントダウンタイマーの製作プログラムです。
## 2. 必要なライブラリ
 液晶とGPSを使用していますのでTinyGPS++とLiquidCrystalのライブラリを追加してください。
## 3. 回路について
 GPSの信号が3.3Vのため、5Vだとレベル変換が必要となるので、Arduino Miniを使用してます。7セグメントの点灯は各桁ごとに制御しています。そのため、出力が多くなるシフトレジスタを使用しています。製作の流れについては[elchika](https://elchika.com/article/36280261-c78f-4055-9312-cedc979a00b0/)に記事を投稿しているので見て頂けたらと思います。
## 4. Eagleファイルの追加
　Eagleのボードファイルをアップしました。もし、回路に興味がある人は見てください。
