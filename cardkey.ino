#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <RCS620S.h>
#include <avr/pgmspace.h>
#include <Servo.h>
#include<SPI.h>
#include <Wire.h>
#include <DS3231.h>

#define LCD_I2C 0x27

const char IDm001[] PROGMEM = "01120412D417FF0A";
const char IDm002[] PROGMEM = "01100310F70F9D12";
const char IDm003[] PROGMEM = "0139847C4787E6F5";
const char* const IDms[] PROGMEM = { IDm001, IDm002, IDm003 };

const char name001[] PROGMEM = "Toshiki Imai";
const char name002[] PROGMEM = "Ryousuke Yagi";
const char name003[] PROGMEM = "Yousuke Yoshizawa";
const char* const names[] PROGMEM = { name001, name002, name003 };

bool *is_enter;
int num_felica;

#define COMMAND_TIMEOUT  400
#define POLLING_INTERVAL 1000
#define SD_CS 53
#define SERVO 3

Servo myservo;
LiquidCrystal_I2C lcd(LCD_I2C, 20, 4);
DS3231 clock;
RTCDateTime dt;
RCS620S rcs620s;
File memory_file, status_file;

void setup() {
  int y = 0, ret = 0, i;
  int ye, mo, da, ho, mi, se;
  char buf[100];
  
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial.println("Serial start");

  // 変数の初期化
  num_felica = sizeof(names) / sizeof(char*);
  is_enter = (bool*) malloc(num_felica * sizeof(bool));
  
  myservo.attach(SERVO);
  myservo.write(90);
  delay(500);
  myservo.detach();
  Serial.println("servo init ok");

  // RTCの初期化
  clock.begin();
  clock.setDateTime(__DATE__, __TIME__);
  Serial.println("Initialize DS3231");
  
  // LCDの初期化
  lcd.init();
  Serial.println("LCD init ok");
  
  lcd.backlight();
  lcd.clear();
  
  // SDの初期化
  lcd.setCursor(0, y);
  pinMode(SD_CS, OUTPUT);
  if (!SD.begin(SD_CS)) {
    lcd.print("SD failed!");
    Serial.println("SD failed!");
    while (1);
  }
  lcd.print("SD init OK");
  Serial.println("SD init OK");
  y++;

  // 入退室情報の初期化
  if(SD.exists("enter.txt")){
    status_file = SD.open("enter.txt");
    status_file.read(buf, num_felica*2);
    status_file.close();
    for(i = 0; i < num_felica*2; i += 2){
      is_enter[i/2] = buf[i] - '0';
    }
  }
  
  // RC-S620Sの初期化
  ret = rcs620s.initDevice();
  while (!ret) {}
  rcs620s.timeout = COMMAND_TIMEOUT;
  lcd.setCursor(0, y);
  lcd.print("RC-S620S init ok");
  Serial.println("RC-S620S init ok");
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
  RTCDateTime t;

  // サーボモータを動かさない間は開放する
  myservo.detach();

  // 時刻の表示
  t = clock.getDateTime();
  lcd.setCursor(0, 0);
  sprintf(date, "%04d/%02d/%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
  lcd.print(date);
  lcd.setCursor(0, 1);
  lcd.print("Wait FeliCa ");
  lcd.setCursor(0, 2);
  lcd.print("                    ");

  // FeliCaのタッチ状態を得る
  ret = rcs620s.polling();
  
  // FeliCaがタッチされた場合
  if(ret){
    sprintf(fname, "%04d%02d%02d", t.year, t.month, t.day);
    strcat(fname, ".csv");
    Serial.println(fname);
    
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
      myservo.write(90);
      
    // 退室
    }else{
      is_enter[i] = false;
      lcd.print(F("Leave       "));

      myservo.attach(SERVO);
      myservo.write(0);
      
    }

    // 名前表示
    lcd.setCursor(0, 2);
    lcd.print(F("                    "));
    lcd.setCursor(0, 2);
    lcd.print(uname);

    Serial.print(IDm_buf);
    Serial.print(",");
    Serial.print(uname);
    Serial.print(",");
    Serial.print(date);
    Serial.print(",");
    Serial.println((is_enter[i] ? "Enter" : "Leave"));

        
    // 入退室記録を書き込み
    memory_file = SD.open(fname, FILE_WRITE);
    if (memory_file){
      memory_file.print(IDm_buf);
      memory_file.print(",");
      memory_file.print(uname);
      memory_file.print(",");
      memory_file.print(date);
      memory_file.print(",");
      memory_file.println((is_enter[i] ? "Enter" : "Leave"));
      memory_file.close();
    }else{
      Serial.println("File open error [memory]");
      memory_file.close();
    }

    // 入退室の状態を書き込み
    // 一度消して更新する
    
    SD.remove("enter.txt");
    status_file = SD.open("enter.txt", FILE_WRITE);
    if (status_file){
      for (i = 0; i < num_felica; i++) {
        status_file.print(is_enter[i]);
        status_file.print(",");
      }
      status_file.println();
      status_file.close();
    }else{
      Serial.println("File open error [status]");
      status_file.close();
    }
  }

  rcs620s.rfOff();
  delay(POLLING_INTERVAL);
}