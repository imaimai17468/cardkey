#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <RCS620S.h>
#include <avr/pgmspace.h>
#include <Servo.h>
#include <SPI.h>
#include <Wire.h>
#include <DS3231.h>

using namespace std;

String IDms[20];

bool is_enter = true;
int num_felica = 0;

#define COMMAND_TIMEOUT  400
#define POLLING_INTERVAL 1000
#define MOTER_INTERVAL 500
#define SD_CS 53
#define SERVO 3
#define LED_OK 23
#define LED_NG 25
#define BUTTON_CARD 45
#define BUTTON_MOTER 43
#define LCD_I2C 0x27

Servo myservo;
LiquidCrystal_I2C lcd(LCD_I2C, 20, 4);
DS3231 clock;
RTCDateTime dt;
RCS620S rcs620s;
File memory_file, status_file, IDm_file;

void setup() {
  int y = 0, ret = 0, i;
  int ye, mo, da, ho, mi, se;
  char buf[100];
  
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial.println("Serial start");
  
  // ピンモードの設定
  pinMode(BUTTON_CARD, INPUT_PULLUP);
  pinMode(BUTTON_MOTER, INPUT_PULLUP);
  pinMode(LED_OK, OUTPUT);
  pinMode(LED_NG, OUTPUT);

  myservo.attach(SERVO);
  myservo.write(90);
  delay(MOTER_INTERVAL);
  myservo.detach();
  Serial.println("servo init ok");

  // RTCの初期化
//  clock.begin();
//  clock.setDateTime(__DATE__, __TIME__);
//  Serial.println("Initialize DS3231");
  
  // LCDの初期化
  lcd.init();
  Serial.println("LCD init ok");
  
  lcd.backlight();
  lcd.clear();
  
  // SDの初期化
  lcd.setCursor(0, y);
  pinMode(SD_CS, OUTPUT);
  if (!SD.begin(SD_CS)) {
    lcd.print("SD failed");
    Serial.println("SD failed! [SD card]");
    while (1);
  }
  lcd.print("SD init OK");
  Serial.println("SD init OK");
  y++;

  // IDm情報の初期化
  if(SD.exists("idm.txt")){
    // IDmを読み込み
    Serial.println("read idm.txt");
    IDm_file = SD.open("idm.txt");
    while(IDm_file.available()){
      String buffer = IDm_file.readStringUntil('\n');
      IDms[num_felica] = buffer;
      IDms[num_felica].trim();
      num_felica++;
    }
    IDm_file.close();
  }
  Serial.print("num_felica : ");
  Serial.println(num_felica);

  // 読みとったIDm情報を表示 (debug)
  for(int i = 0; i < num_felica; i++){
    Serial.println(IDms[i]);
  }
  
  // RC-S620Sの初期化
  ret = rcs620s.initDevice();
  if(!ret){
    Serial.println("RC-S620S init failed");
    lcd.print("RC-S620S init failed");
    while(1) {}
  }
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
  String uname, IDm_buf;
  char buf[20], fname[15], date[30];
  RTCDateTime t;
  int button_state_card = 0, button_state_moter = 0;

  // ボタン情報の読み取り
  button_state_card = digitalRead(BUTTON_CARD);
  button_state_moter = digitalRead(BUTTON_MOTER);

  // LED の初期状態
  digitalWrite(LED_OK, HIGH);
  digitalWrite(LED_NG, LOW);

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

  // ボタン押下時の処理
  // カード登録用のボタンが押された時
  if (button_state_card == LOW){
    Serial.println("button card on");
    digitalWrite(LED_NG, HIGH);
    digitalWrite(LED_OK, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Card Registration");
    lcd.setCursor(0, 1);
    lcd.print("Touch the card");

    // カードがタッチされるか、もう一度カード登録ボタンが押されるとループから抜ける
    bool is_touch = false;
    while(!is_touch){
      is_touch = rcs620s.polling();
      button_state_card = digitalRead(BUTTON_CARD);
      if(button_state_card == LOW){
        Serial.println("cancel Registration");
        delay(POLLING_INTERVAL);
        lcd.clear();
        return;
      }
    }
    
    // カード情報を読み取り
    for(i = 0; i < 8; i++){
      sprintf(buf, "%02X", rcs620s.idm[i]);
      IDm += buf;
    }
    Serial.print("IDm = ");
    Serial.println(IDm);

    // IDmをデータベースと比較する
    for(i = 0; i < num_felica; i++){
      if(IDm == IDms[i]){
        lcd.setCursor(0, 3);
        lcd.print("Already registered");
        Serial.print("Already registered, IDm = ");
        Serial.println(IDm);
        delay(POLLING_INTERVAL);
        lcd.clear();
        return;
      }
    }

    // データベースにない場合は登録する
    if(i == num_felica){
      IDm_file = SD.open("idm.txt", FILE_WRITE);
      IDm_file.println(IDm);
      IDm_file.close();
      lcd.setCursor(0, 3);
      lcd.print("Complete");
      Serial.print("Complete registered, IDm = ");
      Serial.println(IDm);
      IDms[num_felica] = IDm;
      num_felica++;
    }

    delay(POLLING_INTERVAL);
    lcd.clear();
  }

  // サーボモーター用のボタンが押された時
  lcd.setCursor(0, 2);
  if (button_state_moter == LOW){
    Serial.println("button morter on");

    // 鍵を内側から開け閉め出来るようにする
    if(is_enter){  // 開→閉
      is_enter = false;
      myservo.attach(SERVO);
      myservo.write(0);
      Serial.println("servo : 0");
      lcd.print(F("unlock      "));
    }else{        // 閉→開
      is_enter = true;;
      myservo.attach(SERVO);
      myservo.write(90);
      Serial.println("servo : 90");
      lcd.print(F("lock        "));
    }

    delay(MOTER_INTERVAL);
  }

  
  // FeliCaのタッチ状態を得る
  ret = rcs620s.polling();
  
  // FeliCaがタッチされた場合
  if(ret){
    sprintf(fname, "%04d%02d%02d", t.year, t.month, t.day);
    strcat(fname, ".csv");
    Serial.println("open " + String(fname));
    
    // IDmを取得する
    for(i = 0; i < 8; i++){
      sprintf(buf, "%02X", rcs620s.idm[i]);
      IDm += buf;
    }

    // IDmをデータベースと比較する
    uname = "Unknown user";
    for(i = 0; i < num_felica; i++){
      if(IDm == IDms[i]){
        uname = IDm;
        break;
      }
    }

    lcd.setCursor(0, 1);

    // 入退室の処理
    if(i == num_felica){  // データベースにないFeliCaがタッチされた場合
      lcd.print(F("Touch       "));
      Serial.print("Unknown FeliCa: IDm = ");
      Serial.println(IDm);
    }else if(is_enter){   // 退室(開→閉)
      is_enter = false;
      lcd.print(F("unlock      "));
      myservo.attach(SERVO);
      myservo.write(0);
      Serial.println("servo : 0");
    }else{                // 入室(閉→開)
      is_enter = true;
      lcd.print(F("lock        "));
      myservo.attach(SERVO);
      myservo.write(90);
      Serial.println("servo : 90");
    }

    // 名前表示
    lcd.setCursor(0, 2);
    lcd.print(F("                    "));
    lcd.setCursor(0, 2);
    lcd.print(uname);

    Serial.print(uname);
    Serial.print(",");
    Serial.print(date);
    if(uname != "Unknown user"){
      Serial.print(",");
      Serial.println((is_enter ? "Enter" : "Leave"));
    }else{
      Serial.println();
    }

        
    // 入退室記録を書き込み
    memory_file = SD.open(fname, FILE_WRITE);
    if (memory_file){
      memory_file.print(uname);
      memory_file.print(",");
      memory_file.print(date);
      if(uname != "Unknown user"){
        memory_file.print(",");
        memory_file.println((is_enter ? "Enter" : "Leave"));
      }else{
        memory_file.println();
      }
    }else{
      Serial.print(fname);
      Serial.println(" File open error");
    }
    memory_file.close();
   
    delay(POLLING_INTERVAL);
  }

  rcs620s.rfOff();
}