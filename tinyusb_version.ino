#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

int  in[128];
// Sliekšņi notīm oktāvas ietvaros (0-255)
byte NoteV[13] = {8, 23, 40, 57, 76, 96, 116, 138, 162, 187, 213, 241, 255};
float f_peaks[5];
int Mic_pin;

const int POGA_1 = 2; // Ventiļs 1
const int POGA_2 = 3; // Ventiļs 2
const int POGA_3 = 4; // Ventiļs 3

bool stavoklis1 = false, stavoklis2 = false, stavoklis3 = false;
bool ieprieks1 = false, ieprieks2 = false, ieprieks3 = false;

// Rakstītās trompetes harmonikas (MIDI numuri)
// Katrai kombinācijai definējam pilnu sēriju (Partial 2, 3, 4, 5, 6, 8)
// 000 - Open, 001 - 3, 010 - 2, 011 - 2+3, 100 - 1, 101 - 1+3, 110 - 1+2, 111 - 1+2+3
const byte harmonikas[8][7] = {
  {48, 60, 67, 72, 76, 79, 84}, // 000: C3 (pedal), C4, G4, C5, E5, G5, C6
  {45, 57, 64, 69, 73, 76, 81}, // 001: (V3) A3, A4, E5...
  {47, 59, 66, 71, 75, 78, 83}, // 010: (V2) B3, B4, F#5...
  {44, 56, 63, 68, 72, 75, 80}, // 011: (V2+3) Ab3, Ab4, Eb5...
  {46, 58, 65, 70, 74, 77, 82}, // 100: (V1) Bb3, Bb4, F5...
  {43, 55, 62, 67, 71, 74, 79}, // 101: (V1+3) G3, G4, D5...
  {45, 57, 64, 69, 73, 76, 81}, // 110: (V1+2) A3, A4, E5...
  {42, 54, 61, 66, 70, 73, 78}  // 111: (V1+2+3) F#3, F#4, C#5...
};

int lastMidiNote = -1; 

void printNoteName(byte midiNote) {
  const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  Serial.print(names[midiNote % 12]);
  Serial.print(midiNote / 12 - 1); // Oktāva
}

void setup() {
  #if defined(ARDUINO_ARCH_MBED) && defined(ARDUINO_ARCH_RP2040)
    TinyUSB_Device_Init(0);
  #endif
  MIDI.begin(MIDI_CHANNEL_OFF);
  Serial.begin(115200);
  analogReadResolution(12);
  Mic_pin = A0;
  pinMode(Mic_pin, INPUT);
  pinMode(POGA_1, INPUT_PULLUP);
  pinMode(POGA_2, INPUT_PULLUP);
  pinMode(POGA_3, INPUT_PULLUP);
}

void loop() {
  stavoklis1 = (digitalRead(POGA_1) == LOW);
  stavoklis2 = (digitalRead(POGA_2) == LOW);
  stavoklis3 = (digitalRead(POGA_3) == LOW);

  // Ja mainās ventiļi, pārtraucam iepriekšējo noti
  if (stavoklis1 != ieprieks1 || stavoklis2 != ieprieks2 || stavoklis3 != ieprieks3) {
    if (lastMidiNote >= 0) {
      MIDI.sendNoteOff(lastMidiNote, 0, 1);
      lastMidiNote = -1;
    }
    ieprieks1 = stavoklis1; ieprieks2 = stavoklis2; ieprieks3 = stavoklis3;
  }

  Tone_det();
  delay(10);
}

void Tone_det() {
  unsigned long a1, b;
  float a, sum1 = 0, sum2 = 0, sampling;

  a1 = micros();
  for (int i = 0; i < 128; i++) {
    a = (float)(analogRead(Mic_pin) - 2048);
    sum1 += a; sum2 += a * a;
    a = a * (sin(i * 3.14159 / 128) * sin(i * 3.14159 / 128));
    in[i] = (int)(10 * a);
    delayMicroseconds(195);
  }
  b = micros();
  sum1 /= 128;
  sum2 = sqrt(sum2 / 128);
  sampling = 128000000.0 / (b - a1);

  if (sum2 - sum1 > 5.0) { // Ja ir skaņa
    FFT(128, sampling);

    // Mēs meklēsim spēcīgāko pīķi un noteiksim tā absolūto MIDI noti
    float max_peak_freq = f_peaks[0]; 
    if (max_peak_freq < 60) return; // Pārāk zems

    // Aprēķinām MIDI noti no frekvences: 12 * log2(f/440) + 69
    int detektetaNote = round(12.0 * log2(max_peak_freq / 440.0) + 69.0);
    
    // Pārejam uz "rakstīto" noti (trompete Bb: rakstītā ir par 2 pustoņiem augstāka)
    int rakstitaNotis = detektetaNote + 2;

    // Noskaidrojam ventiļu kombināciju
    byte comb = (stavoklis1 ? 4 : 0) | (stavoklis2 ? 2 : 0) | (stavoklis3 ? 1 : 0);
    
    int labakaNotis = -1;
    int mazakaStarpiba = 99;

    // Atrodam tuvāko harmoniku no sērijas
    for (int n = 0; n < 7; n++) {
      int h = harmonikas[comb][n];
      int diff = abs(rakstitaNotis - h);
      if (diff < mazakaStarpiba) {
        mazakaStarpiba = diff;
        labakaNotis = h;
      }
    }

    // Ja starpība ir saprātīga (piem. 2 pustoņi), spēlējam
    if (labakaNotis >= 0 && mazakaStarpiba <= 2) {
      int concertMidi = labakaNotis - 2; // Koncertskaņa MIDI izvadei

      if (concertMidi != lastMidiNote) {
        if (lastMidiNote >= 0) MIDI.sendNoteOff(lastMidiNote, 0, 1);
        
        MIDI.sendNoteOn(concertMidi, 127, 1);
        Serial.print("Rakstītā: "); printNoteName(labakaNotis);
        Serial.print(" -> MIDI: "); Serial.println(concertMidi);
        lastMidiNote = concertMidi;
      }
    }
  } else if (lastMidiNote >= 0) {
    MIDI.sendNoteOff(lastMidiNote, 0, 1);
    lastMidiNote = -1;
  }
}

// FFT funkcija paliek nemainīta, bet pārliecinies, ka f_peaks[0] ir galvenā frekvence
// ======================= FFT (nemainīta) ====================================
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
