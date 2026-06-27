//---------------------------------------------------------------------------//
// Trompetes Bb transponējošais kvantizētājs (3 ventiļi + mikrofons)
// Darbība:
//   1. FFT nosaka koncertskaņas noti (indekss 0..11).
//   2. Pārveido to par trompetes rakstīto noti: +2 pustoņi (Bb -> C).
//   3. Nolasa 3 ventiļus un izvēlas attiecīgo virsskaņu kopu.
//   4. Sameklē tuvāko atļauto harmoniku ar pielaidi ±1 pustonis.
//   5. Izvada kvantizēto noti.
// Bez ventiļiem (0,0,0) atļautās harmonikas: C, E, G.
//---------------------------------------------------------------------------//

// --- Globālie mainīgie (FFT / tonis) ----------------------------------------
int  in[128];
byte NoteV[13] = {8, 23, 40, 57, 76, 96, 116, 138, 162, 187, 213, 241, 255};
float f_peaks[5];
int Mic_pin;

// --- Ventiļu tapas ----------------------------------------------------------
const int POGA_1 = 2;   // 1. ventilis (kreisais)  GPIO2 (Pin 4)
const int POGA_2 = 3;   // 2. ventilis (vidējais)  GPIO3 (Pin 5)
const int POGA_3 = 4;   // 3. ventilis (labais)    GPIO4 (Pin 6)

bool stavoklis1 = false;
bool stavoklis2 = false;
bool stavoklis3 = false;
bool ieprieks1 = false;
bool ieprieks2 = false;
bool ieprieks3 = false;

// --- Virsskaņu kopas atbilstoši ventiļu kombinācijām ------------------------
// Indekss = (1.ventilis * 4) + (2.ventilis * 2) + (3.ventilis * 1)
// Katrai kombinācijai 3 rakstītās trompetes notis (MIDI stila indeksi: 0=C...11=B)
const byte harmonikas[8][3] = {
  { 0, 4, 7 },   // 0 (000) : C, E, G
  { 9, 4, 1 },   // 1 (001) : A, E, C#
  {11, 6, 3 },   // 2 (010) : B, F#, D#
  { 8, 3, 0 },   // 3 (011) : Ab, Eb, C
  {10, 5, 2 },   // 4 (100) : Bb, F, D
  { 7, 2,11 },   // 5 (101) : G, D, B
  { 9, 4, 1 },   // 6 (110) : A, E, C#
  { 6, 1,10 }    // 7 (111) : F#, C#, A#
};

// ======================= SETUP ==============================================
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);            // Pico ADC 12 bitu
  Mic_pin = A0;                        // GPIO26
  pinMode(Mic_pin, INPUT);

  pinMode(POGA_1, INPUT_PULLUP);
  pinMode(POGA_2, INPUT_PULLUP);
  pinMode(POGA_3, INPUT_PULLUP);

  Serial.println("Trompetes Bb kvantizētājs gatavs!");
}

// ======================= LOOP ===============================================
void loop() {
  // 1. Nolasa ventiļus
  stavoklis1 = (digitalRead(POGA_1) == LOW);
  stavoklis2 = (digitalRead(POGA_2) == LOW);
  stavoklis3 = (digitalRead(POGA_3) == LOW);

  // Ja mainījies stāvoklis, izvada kombināciju
  if (stavoklis1 != ieprieks1 || stavoklis2 != ieprieks2 || stavoklis3 != ieprieks3) {
    Serial.print("Ventiļi: ");
    Serial.print(stavoklis1 ? "[X] " : "[O] ");
    Serial.print(stavoklis2 ? "[X] " : "[O] ");
    Serial.print(stavoklis3 ? "[X]"  : "[O]");
    Serial.println();
    ieprieks1 = stavoklis1;
    ieprieks2 = stavoklis2;
    ieprieks3 = stavoklis3;
  }

  // 2. Veic toņa detektēšanu un kvantizēšanu
  Tone_det();
  delay(10);
}

// ======================= TOŅA DETEKCIJA UN KVENTIZĒŠANA ======================
void Tone_det() {
  unsigned long a1, b;
  float a;
  float sum1 = 0, sum2 = 0;
  float sampling;

  // Paraugu iegūšana
  a1 = micros();
  for (int i = 0; i < 128; i++) {
    a = (float)(analogRead(Mic_pin) - 2048);
    sum1 += a;
    sum2 += a * a;
    a = a * (sin(i * 3.14159 / 128) * sin(i * 3.14159 / 128));
    in[i] = (int)(10 * a);
    delayMicroseconds(195);
  }
  b = micros();

  sum1 /= 128;
  sum2 = sqrt(sum2 / 128);
  sampling = 128000000.0 / (b - a1);

  // Tikai tad, ja signāls ir pietiekami skaļš
  if (sum2 - sum1 > 3.0) {
    FFT(128, sampling);

    // Notīm atbilstošo balsu skaitītājs (in[] izmantosim kā balsu masīvu)
    for (int i = 0; i < 12; i++) in[i] = 0;

    int j = 0, k = 0;
    for (int i = 0; i < 5; i++) {
      if (f_peaks[i] > 1040) f_peaks[i] = 0;
      if (f_peaks[i] >= 65.4  && f_peaks[i] <= 130.8)  f_peaks[i] = 255 * ((f_peaks[i] / 65.4) - 1);
      if (f_peaks[i] >= 130.8 && f_peaks[i] <= 261.6)  f_peaks[i] = 255 * ((f_peaks[i] / 130.8) - 1);
      if (f_peaks[i] >= 261.6 && f_peaks[i] <= 523.25) f_peaks[i] = 255 * ((f_peaks[i] / 261.6) - 1);
      if (f_peaks[i] >= 523.25 && f_peaks[i] <= 1046)  f_peaks[i] = 255 * ((f_peaks[i] / 523.25) - 1);
      if (f_peaks[i] >= 1046   && f_peaks[i] <= 2093)  f_peaks[i] = 255 * ((f_peaks[i] / 1046) - 1);
      if (f_peaks[i] > 255) f_peaks[i] = 254;

      j = 1; k = 0;
      while (j == 1) {
        if (f_peaks[i] < NoteV[k]) { f_peaks[i] = k; j = 0; }
        k++;
        if (k > 15) j = 0;
      }
      if (f_peaks[i] == 12) f_peaks[i] = 0;

      k = (int)f_peaks[i];
      in[k] += (5 - i);
    }

    // Dominējošā koncertskaņas nots (indekss 0..11)
    k = 0; j = 0;
    for (int i = 0; i < 12; i++) {
      if (k < in[i]) { k = in[i]; j = i; }
    }

    // ---------- Transponēšana: koncertskaņa -> trompetes rakstītā nots ----------
    int rakstitaNotis = (j + 2) % 12;  // +2 pustoņi (Bb trompete)

    // ---------- Ventiļu kombinācija un kvantizēšana -------------------------
    byte comb = (stavoklis1 ? 4 : 0) | (stavoklis2 ? 2 : 0) | (stavoklis3 ? 1 : 0);
    const byte* notis = harmonikas[comb];
    int labakaNotis = -1;
    int mazakaKjuda = 99;

    for (int n = 0; n < 3; n++) {
      int diff = (rakstitaNotis - notis[n] + 12) % 12;
      if (diff > 6) diff = 12 - diff;   // attālums 0..6
      if (diff <= 1 && diff < mazakaKjuda) {
        mazakaKjuda = diff;
        labakaNotis = notis[n];
      }
    }

    // Izvada tikai tad, ja kvantizēšana izdevusies
    if (labakaNotis >= 0) {
      Serial.print("Kvantizēts: ");
      switch (labakaNotis) {
        case 0:  Serial.println("C"); break;
        case 1:  Serial.println("C# / Db"); break;
        case 2:  Serial.println("D"); break;
        case 3:  Serial.println("D# / Eb"); break;
        case 4:  Serial.println("E"); break;
        case 5:  Serial.println("F"); break;
        case 6:  Serial.println("F# / Gb"); break;
        case 7:  Serial.println("G"); break;
        case 8:  Serial.println("G# / Ab"); break;
        case 9:  Serial.println("A"); break;
        case 10: Serial.println("A# / Bb"); break;
        case 11: Serial.println("B"); break;
      }
    }
    // Pretējā gadījumā neizvada neko (signāls neatbilst nevienai atļautajai notij)
  }
}

// ======================= FFT FUNKCIJA (oriģināla) ============================
float FFT(byte N, float Frequency) {
  byte data[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  int a, c1, f, o, x;
  a = N;

  for (int i = 0; i < 8; i++) {
    if (data[i] <= a) o = i;
  }
  o = 7;
  byte in_ps[data[o]] = {};
  float out_r[data[o]] = {};
  float out_im[data[o]] = {};

  x = 0;
  for (int b = 0; b < o; b++) {
    c1 = data[b];
    f = data[o] / (c1 + c1);
    for (int j = 0; j < c1; j++) {
      x = x + 1;
      in_ps[x] = in_ps[j] + f;
    }
  }

  for (int i = 0; i < data[o]; i++) {
    if (in_ps[i] < a) out_r[i] = in[in_ps[i]];
    if (in_ps[i] > a) out_r[i] = in[in_ps[i] - a];
  }

  int i10, i11, n1;
  float e, c, s, tr, ti;

  for (int i = 0; i < o; i++) {
    i10 = data[i];
    i11 = data[o] / data[i + 1];
    e = 6.283 / data[i + 1];
    e = 0 - e;
    n1 = 0;

    for (int j = 0; j < i10; j++) {
      c = cos(e * j);
      s = sin(e * j);
      n1 = j;

      for (int k = 0; k < i11; k++) {
        tr = c * out_r[i10 + n1] - s * out_im[i10 + n1];
        ti = s * out_r[i10 + n1] + c * out_im[i10 + n1];

        out_r[n1 + i10] = out_r[n1] - tr;
        out_r[n1] = out_r[n1] + tr;

        out_im[n1 + i10] = out_im[n1] - ti;
        out_im[n1] = out_im[n1] + ti;

        n1 = n1 + i10 + i10;
      }
    }
  }

  for (int i = 0; i < data[o - 1]; i++) {
    out_r[i] = sqrt((out_r[i] * out_r[i]) + (out_im[i] * out_im[i]));
    out_im[i] = (i * Frequency) / data[o];
  }

  x = 0;
  for (int i = 1; i < data[o - 1] - 1; i++) {
    if (out_r[i] > out_r[i - 1] && out_r[i] > out_r[i + 1]) {
      in_ps[x] = i;
      x = x + 1;
    }
  }

  float s_val = 0;
  int c_val = 0;
  for (int i = 0; i < x; i++) {
    for (int j = c_val; j < x; j++) {
      if (out_r[in_ps[i]] < out_r[in_ps[j]]) {
        s_val = in_ps[i];
        in_ps[i] = in_ps[j];
        in_ps[j] = (byte)s_val;
      }
    }
    c_val = c_val + 1;
  }

  for (int i = 0; i < 5; i++) {
    f_peaks[i] = (out_im[in_ps[i] - 1] * out_r[in_ps[i] - 1] +
                  out_im[in_ps[i]] * out_r[in_ps[i]] +
                  out_im[in_ps[i] + 1] * out_r[in_ps[i] + 1]) /
                 (out_r[in_ps[i] - 1] + out_r[in_ps[i]] + out_r[in_ps[i] + 1]);
  }
  return 0;
}