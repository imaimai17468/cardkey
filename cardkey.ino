#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <RCS620S.h>
#include <avr/pgmspace.h>
#include <Servo.h>

#define LCD_I2C 0x27

const char IDm001[] PROGMEM = "YourID";
const char* const IDms[] PROGMEM = { IDm001 };

const char name001[] PROGMEM = "YourNAME";
const char* const names[] PROGMEM = { name001 };

bool *is_enter;
int num_felica;

#define COMMAND_TIMEOUT  400
#define POLLING_INTERVAL 1000
#define SD_CS 53
#define SERVO 3

Servo myservo;
LiquidCrystal_I2C lcd(LCD_I2C, 20, 4);
RTC_DS3231 rtc;
RCS620S rcs620s;

// 日付の文字列から年などを取得
int convToNum(char* buf, int start, int digit){
  int i, num = 0, pow = 1, pos;

  pos = start + digit - 1;
  for (i = 0; i < digit; i++) {
    num += (buf[pos] - '0') * pow;
    pow *= 10;
    pos--;
  }
  return num;
}

void setup() {
  int y = 0, ret = 0, i;
  int ye, mo, da, ho, mi, se;
  char buf[100];
  
  Serial.begin(115200);
  Serial1.begin(115200);

  // 変数の初期化
  num_felica = sizeof(names) / sizeof(char*);
  is_enter = (bool*) malloc(num_felica * sizeof(bool));
  
  myservo.attach(SERVO);
  myservo.write(0);
  delay(500);
  myservo.detach();
  
  // LCDの初期化
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  // SDの初期化
  lcd.setCursor(0, y);
  if (!SD.begin(SD_CS)) {
    lcd.print("SD failed!");
    while (1);
  }
  lcd.print("SD init OK");
  y++;

  // 入退室情報の初期化
  if(SD.exists("enter.txt")){
    File f = SD.open("enter.txt");
    f.read(buf, num_felica*2);
    f.close();
    for(i = 0; i < num_felica*2; i += 2){
      is_enter[i/2] = buf[i] - '0';
    }
  }

  // 日時の初期化
  if(SD.exists("time.txt")){
    File f = SD.open("time.txt");
    f.read(buf, 29);
    f.close();
    ye = convToNum(buf, 0, 4);
    mo = convToNum(buf, 5, 2);
    da = convToNum(buf, 8, 2);
    ho = convToNum(buf, 11, 2);
    mi = convToNum(buf, 14, 2);
    se = convToNum(buf, 17, 2);
    //setTime(ho, mi, se, da, mo, ye);
    rtc.adjust(DateTime(ye, mo, da, ho, mi, se));
    SD.remove("time.txt");
    lcd.setCursor(0, y);
    lcd.print("Set time");
    y++;
    lcd.setCursor(0, y);
    sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d", ye, mo, da, ho, mi, se);
    lcd.print(buf);
    y++;
  }
  
  // RC-S620Sの初期化
  ret = rcs620s.initDevice();
  while (!ret) {}
  rcs620s.timeout = COMMAND_TIMEOUT;
  lcd.setCursor(0, y);
  lcd.print("RC-S620S init ok");
  y++;

  // 初期表示
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Wait FeliCa");
}

void loop() {
  int ret, i;
  String IDm = "";
  char buf[30], fname[15], uname[30], IDm_buf[17], date[30];
  DateTime t;

  // サーボモータを動かさない間は開放する
  myservo.detach();

  // 時刻の表示
  t = rtc.now();
  lcd.setCursor(0, 0);
  sprintf(date, "%04d/%02d/%02d %02d:%02d:%02d", t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  lcd.print(date);
  lcd.setCursor(0, 1);
  lcd.print("Wait FeliCa ");
  lcd.setCursor(0, 2);
  lcd.print("                    ");

  // FeliCaのタッチ状態を得る
  ret = rcs620s.polling();
  
  // FeliCaがタッチされた場合
  if(ret){
    sprintf(fname, "%04d%02d%02d", t.year(), t.month(), t.day());
    strcat(fname, ".csv");
    
    // IDmを取得する
    for(i = 0; i < 8; i++){
      sprintf(buf, "%02X", rcs620s.idm[i]);
      IDm += buf;
    }

    // IDmをデータベースと比較する
    strcpy(uname, "Unknown user");
    for(i = 0; i < num_felica; i++){
      strcpy_P(IDm_buf, (char*) pgm_read_word(&IDms[i]));
      if(IDm.compareTo(IDm_buf) == 0){
        strcpy_P(uname, (char*) pgm_read_word(&names[i]));
        break;
      }
    }

    lcd.setCursor(0, 1);
    // データベースにないFeliCaがタッチされた場合
    if(i == num_felica){
      lcd.print(F("Touch       "));
      Serial.print("Unknown FeliCa: IDm = ");
      Serial.println(IDm);

    // 入室
    }else if(is_enter[i] == false){
      is_enter[i] = true;
      lcd.print(F("Enter       "));

      myservo.attach(SERVO);
      myservo.write(0);
      
    // 退室
    }else{
      is_enter[i] = false;
      lcd.print(F("Leave       "));

      myservo.attach(SERVO);
      myservo.write(90);
      
    }

    // 名前表示
    lcd.setCursor(0, 2);
    lcd.print(F("                    "));
    lcd.setCursor(0, 2);
    lcd.print(uname);

    // 入退室記録を書き込み
    File f = SD.open(fname, FILE_WRITE);
    if (f){
      for (i = 0; i < num_felica; i++) {
        strcpy_P(IDm_buf, (char*) pgm_read_word(&IDms[i]));
        strcpy_P(uname, (char*) pgm_read_word(&names[i]));
        f.print(IDm_buf);
        f.print(",");
        f.print(uname);
        f.print(",");
        f.print(date);
        f.print(",");
        f.println((is_enter[i] ? "Enter" : "Leave"));

        Serial.print(IDm_buf);
        Serial.print(",");
        Serial.print(uname);
        Serial.print(",");
        Serial.print(date);
        Serial.print(",");
        Serial.println((is_enter[i] ? "Enter" : "Leave"));
      }
      f.close();
    }else{
      Serial.println("File open error");
    }

    // 入退室の状態を書き込み
    // 一度消して更新する
    SD.remove("enter.txt");
    f = SD.open("enter.txt", FILE_WRITE);
    if (f){
      for (i = 0; i < num_felica; i++) {
        f.print(is_enter[i]);
        f.print(",");
      }
      f.println();
      f.close();
    }else{
      Serial.println("File open error");
    }
  }

  rcs620s.rfOff();
  delay(POLLING_INTERVAL);
}
