/*
*
*    clocks
*
*
*/

const int8_t MODES = 6;

enum CLOCKMODES 
{
  LFSR,
  RANDOM,
  DIV,
  EUCLID,
  LOGIC,
  DAC

};

enum _BPM_SEL 
{  
  _4TH, 
  _8TH,
  _16TH
  
};

extern const uint32_t BPM_microseconds_4th[];
extern const uint32_t BPM_microseconds_8th[];
extern const uint32_t BPM_microseconds_16th[];

const uint8_t MULT_LIMIT = 4; // == *16, *8, *4, *2, *1
const uint16_t tick_thresholds[MULT_LIMIT+1] = {0xFFFF, 8, 4, 2, 0}; 

const int8_t CHANNELS = 6;     // # channels
uint32_t SUB_PERIOD = 0xFFFFFFFF;    // mult.

volatile uint16_t CLK_SRC = false; // clock source: ext/int
volatile uint16_t _OK = 0x0;       // ext. clock flag

uint16_t BPM = 100;            // int. clock
uint16_t BPM_SEL = 0;          // 1/4, 1/8, 1/16
const uint8_t  BPM_MIN = 8;
const uint16_t BPM_MAX[] = { 320, 320, 200};
uint32_t BPM_MICROSEC = BPM_microseconds_4th[BPM - BPM_MIN]; ; // BPM_CONST/BPM;
uint32_t CORE_TIMER;
uint8_t INIT_MODE = 2; // initial mode: 2 => mult/div

uint16_t DAC_OUT = 0;
const uint16_t _ON = 4000;
const uint16_t _ZERO[2] = {1800, 0}; // DAC 0.0V
const uint16_t DAC_CHANNEL = 3;

const uint16_t PULSE_WIDTH = 0;


extern uint16_t MENU_REDRAW;

struct params {

  uint8_t    mode;               // LFRS, RANDOM, MULT/DIV, EUCLID, LOGIC, DAC*
  uint8_t    channel_modes;      // modes per channel: {5, 5, 5, 5, 5, 5+DAC };
  uint8_t    mode_param_numbers; // 3, 3, 2, 3, 3, 2
  uint16_t   param[6][4];        // param values or submode: param[<mode>][<param>];
  uint16_t   param_min[4];       // limits min
  uint16_t   param_max[4];       // limits max
  uint8_t    cvmod[5];           // assigned param [none, param1, param2, param3, param4]
  uint32_t   lfsr;               // data
  uint8_t    _pw;                // pulse_width
  uint8_t    _mult;              // mult/div factor
  uint8_t    _ticks;             // sub clock
  uint8_t    _state;             // on/off?
  uint32_t   timestamp;          // --> clocksoff
  uint32_t   cnt;                // clocks count
};

const uint8_t  _CHANNEL_MODES[]  = {5, 5, 5, 6, 5, 5}; // modes per channel; ch 4 = 6, ie. 5 + DAC
const uint8_t  _CHANNEL_PARAMS[] = {3, 2, 2, 3, 3, 2}; // [len, tap1, tap2]; [rand(N), direction]; [div, direction]; [N, K, offset]; [type, op1, *op2]; [scale, polarity]
const uint16_t _CHANNEL_PARAMS_MIN[MODES][4] = {
  // pw, p0, p1, p3
  {5, 4, 0, 0}, // lfsr
  {5, 1, 0, 0}, // rnd
  {5, 0, 0, 0}, // div
  {5, 2, 1, 0}, // euclid
  {5, 0, 1, 1}, // logic
  {0, 0, 0, 0} // dac
};
const uint16_t _CHANNEL_PARAMS_MAX[MODES][4] = {
  // pw, p0, p1, p3
  {150, 31, 31, 31},
  {150, 31, 1, 0},
  {150, 31, 1, 0},
  {150, 31, 31, 15},
  {150, 4, 6, 6},
  {1, 31, 1, 0}
};

params allChannels[6];

/* ------------------------------------------------------------------   */
void bpm_set_microseconds() {
  switch (BPM_SEL) {

      case _4TH:  BPM_MICROSEC = BPM_microseconds_4th[BPM-BPM_MIN];  break;
      case _8TH:  BPM_MICROSEC = BPM_microseconds_8th[BPM-BPM_MIN];  break;
      case _16TH: BPM_MICROSEC = BPM_microseconds_16th[BPM-BPM_MIN]; break;
      default: break;
  }
}

/* ------------------------------------------------------------------   */
void channel_set_mode(struct params* p, uint8_t mode) {
  
  p->mode = mode;
  p->mode_param_numbers = _CHANNEL_PARAMS[mode];
  const uint16_t *_min_ptr = _CHANNEL_PARAMS_MIN[mode];
  const uint16_t *_max_ptr = _CHANNEL_PARAMS_MAX[mode];
  memcpy(p->param_min, _min_ptr, sizeof(p->param_min));
  memcpy(p->param_max, _max_ptr, sizeof(p->param_max));
}

/* ------------------------------------------------------------------   */
void clocks_restore_channel(struct params* p, const struct channel_settings* settings) {
  
  channel_set_mode(p, settings->mode);
  memcpy(p->param[p->mode], settings->param, sizeof(settings->param));
  memcpy(p->cvmod, settings->cvmod, sizeof(p->cvmod));
}

/* ------------------------------------------------------------------   */
void clocks_store_channel(const struct params* p, struct channel_settings* settings) {
  
  settings->mode = p->mode;
  memcpy(settings->param, p->param[p->mode], sizeof(settings->param));
  memcpy(settings->cvmod, p->cvmod, sizeof(p->cvmod));
}

/* ------------------------------------------------------------------   */
void clocks_store(struct settings_data *settings) {

  settings->clk_src = (uint8_t)CLK_SRC;
  settings->bpm = BPM;
  settings->bpm_sel = (uint8_t)BPM_SEL;

  for (int i  = 0; i < 6; i++) {
    clocks_store_channel(&allChannels[i], &settings->channels[i]);
  }
}

/* ------------------------------------------------------------------   */
void clocks_restore(const struct settings_data *settings) {
  
  CLK_SRC = settings->clk_src;
  BPM = settings->bpm;
  BPM_SEL = settings->bpm_sel;
  bpm_set_microseconds();

  for (int i  = 0; i < 6; i++) {
    clocks_restore_channel(&allChannels[i], &settings->channels[i]);
  }
}

/* ------------------------------------------------------------------   */

void init_channel(struct params* _p, uint8_t _channel) {
  
    channel_set_mode(_p, INIT_MODE);
    _p->channel_modes = _CHANNEL_MODES[_channel];
    _p->lfsr = random(0xFFFFFFFF);
  
    for (int i = 0; i < MODES; i++) {
  
      _p->param[i][0] = 50; // pw
      _p->param[i][1] = _CHANNEL_PARAMS_MIN[i][1];
      _p->param[i][2] = _CHANNEL_PARAMS_MIN[i][2];
      _p->param[i][3] = _CHANNEL_PARAMS_MIN[i][3];
    }
    _p->cvmod[0] = 0;
    _p->cvmod[1] = 0;
    _p->cvmod[2] = 0;
    _p->cvmod[3] = 0;
    _p->cvmod[4] = 0;
    _p->param[DAC][1] = 15;  // init DAC mult. param
    _p->_pw = 50;            // pw [effective]
    _p->_mult = 0;           // = * 1
}

/* ------------------------------------------------------------------   */

void init_clocks() {

  for (int i  = 0; i < CHANNELS; i++) {

    init_channel(&allChannels[i], i);
  }
}

/* ------------------------------------------------------------------   */

uint16_t coretimer() {

    // main clock
    if (_OK) { 
        
        allChannels[0]._ticks = 0;
        allChannels[1]._ticks = 0;
        allChannels[2]._ticks = 0;
        allChannels[3]._ticks = 0;
        allChannels[4]._ticks = 0;
        allChannels[5]._ticks = 0;
        
        // recalc. sub timer: ext. int. * 16
        SUB_PERIOD = (uint32_t)(1.0f+(float)TIME_STAMP*0.0625f);
        _OK = subticks = 0;
        return 1;
    }
    // subticks (main clock * 16)
    else if (subticks >= SUB_PERIOD) { 
      
        allChannels[0]._ticks++;
        allChannels[1]._ticks++;
        allChannels[2]._ticks++;
        allChannels[3]._ticks++;
        allChannels[4]._ticks++;
        allChannels[5]._ticks++;
        subticks = 0; // reset
        return 1;
    }
    else return 0;
}


uint8_t gen_next_clock(struct params* _p, uint8_t _ch)   {

  if (!digitalReadFast(TR2)) sync(_ch);

  switch (_p->mode) {

    case LFSR:   return _lfsr(_p); 
    case RANDOM: return _rand(_p); 
    case DIV:    return _plainclock(_p); 
    case EUCLID: return _euclid(_p); 
    case LOGIC:  return (_p->_state >> _ch) & 1u; // logic: return prev. value
    case DAC:    return _dac(_p);
    default:     return 0xFF;
  }
}


/* ------------------------------------------------------------------   */

void output_clocks(uint8_t _clk_state) {  // update clock outputs - atm, this is called either by the ISR or coretimer()

  uint16_t clk = _clk_state;
 
  digitalWriteFast(CLK1, clk & 0x1);
  digitalWriteFast(CLK2, clk & 0x2);
  digitalWriteFast(CLK3, clk & 0x4);
//digitalWriteFast(CLK4, clk & 0x8); // --> DAC, see below
  digitalWriteFast(CLK5, clk & 0x10);
  digitalWriteFast(CLK6, clk & 0x20);
  analogWrite(A14, DAC_OUT);
}

/* ------------------------------------------------------------------   */


 
uint8_t next_clocks() {

    uint8_t _clk_state = 0x00, _sub_clocks = 0x00;
     
    // 0. update sub-clocks:

    for (int i  = 0; i < CHANNELS; i++) {

        uint16_t _tick = allChannels[i]._ticks;
        uint16_t m = tick_thresholds[allChannels[i]._mult]; // 0 = * 1  ... 4 = * 16
 
        if (!_tick || _tick >= m)  {  // set clock + reset counter
          
            _sub_clocks |=  (1 << i);
            allChannels[i]._ticks = 0;
            allChannels[i].cnt++;
            _OK = 0x0;
        }
    }

    // 1. update clocks:
    for (int i  = 0; i < CHANNELS; i++) {
            
            uint16_t tmp = 0;
            // needs updating ? 
            if ((_sub_clocks >> i) & 1u) tmp = gen_next_clock(&allChannels[i], i);
      
            if (tmp)  _clk_state |=  (1 << i);    // set clock
            else      _clk_state &= ~(1 << i);    // clear clock
            update_pw(&allChannels[i]);           // update pw
    }

    // 2. apply logic:
    for (int i  = 0; i < CHANNELS; i++) {
  
        if (allChannels[i].mode == LOGIC)  {
            uint16_t tmp = _logic(&allChannels[i], _clk_state);
            if (tmp)  _clk_state |=  (1 << i); // set clock
            else      _clk_state &= ~(1 << i); // clear clock
        }
    }
  
    // 3. calc. next DAC code: 
    if (allChannels[DAC_CHANNEL].mode == DAC) outputDAC(&allChannels[DAC_CHANNEL], _clk_state);
    else DAC_OUT = (_clk_state & 0x8) ? _ON : _ZERO[0];
    
    // 4. output clocks +  clocksoff + redraw menu :   
    if (_clk_state) {
      
            output_clocks(_clk_state); 
           
            uint32_t now = millis();
            
            // update states + timestamps --> clocksoff
            if (!allChannels[0]._state && _clk_state & 0x1) { 

                    allChannels[0]._state  = 1;
                    allChannels[0].timestamp = now;
              
            }
            if (!allChannels[1]._state && _clk_state & 0x2) { 
              
                    allChannels[1]._state  = 1;
                    allChannels[1].timestamp = now;
              
            }
            if (!allChannels[2]._state && _clk_state & 0x4) { 
              
                    allChannels[2]._state  = 1;
                    allChannels[2].timestamp = now;
              
            }
            if (!allChannels[3]._state && _clk_state & 0x8) { 
              
                    allChannels[3]._state  = 1;
                    allChannels[3].timestamp = now;
              
            }
            if (!allChannels[4]._state && _clk_state & 0x10) { 
              
                    allChannels[4]._state  = 1;
                    allChannels[4].timestamp = now;
              
            }
            if (!allChannels[5]._state && _clk_state & 0x20) { 
              
                    allChannels[5]._state  = 1;
                    allChannels[5].timestamp = now;
              
            }
            // update display clock  
            display_clock = _clk_state; 
            MENU_REDRAW = 1;
    }
 
    return _clk_state;
}



/*  --------------------------- the clock modes --------------------------    */

// 1 : lfsr

uint8_t _lfsr(struct params* _p) {

  uint8_t _mode, _tap1, _tap2, _len, _out;
  int16_t _cv1, _cv2, _cv3;
  uint32_t _data;
  _mode = LFSR;

  _cv1 = _p->cvmod[2];         // len CV
  _cv2 = _p->cvmod[3];         // tap1 CV
  _cv3 = _p->cvmod[4];         // tap2 CV
  _len  = _p->param[_mode][1]; // len param
  _tap1 = _p->param[_mode][2]; // tap1 param
  _tap2 = _p->param[_mode][3]; // tap2 param

  /* cv mod ? */
  if (_cv1) { // len

    _cv1 = (HALFSCALE - CV[_cv1]) >> 4;
    _len = limits(_p, 1, _cv1 + _len);
  }
  if (_cv2) { // tap1

    _cv2 = (HALFSCALE - CV[_cv2]) >> 5;
    _tap1 = limits(_p, 2, _cv2 + _tap1);
  }
  if (_cv3) { // tap2

    _cv3 = (HALFSCALE - CV[_cv3]) >> 5;
    _tap2 = limits(_p, 3, _cv3 + _tap2);
  }

  _data = _p->lfsr;
  _out = _data & 1u;

  _tap1 = (_data >> _tap1) & 1u; // bit at tap1
  _tap2 = (_data >> _tap2) & 1u; // bit at tap2
  _p->lfsr = (_data >> 1) | ((_out ^ _tap1 ^ _tap2) << (_len - 1)); // update lfsr
  if (_data == 0x0 || _data == 0xFFFFFFFF) _p->lfsr = random(0xFFFFFFFF); // let's not get stuck (entirely)
  return _out;
}

// 2 : random

uint8_t _rand(struct params* _p) {

  uint8_t _mode, _n, _dir, _out;
  int16_t _cv1, _cv2;
  _mode = RANDOM;

  _cv1 = _p->cvmod[2]; // RND-N CV
  _cv2 = _p->cvmod[3]; // DIR CV
  _n   = _p->param[_mode][1]; // RND-N param
  _dir = _p->param[_mode][2]; // DIR param

  /* cv mod ? */
  if (_cv1) {
    _cv1 = (HALFSCALE - CV[_cv1]) >> 5;
    _n = limits(_p, 1, _cv1 + _n);
  }
  if (_cv2)  { // direction :
    _cv2 = CV[_cv2];
    if (_cv2 < THRESHOLD) _dir =  ~_dir & 1u;
  }

  _out = random(_n + 1);
  _out &= 1u;             // 0 or 1
  if (_dir) _out = ~_out; //  if inverted: 'yes' ( >= 1)
  return _out & 1u;
}

// 3 : clock div

uint8_t _plainclock(struct params* _p) {

  uint16_t _mode, _n, _out;
  int16_t _cv1, _cv2, _div, _dir;
  _mode = DIV;

  _cv1 = _p->cvmod[2]; // div CV
  _cv2 = _p->cvmod[3]; // dir CV
  _div = _p->param[_mode][1]; // div param
  _dir = _p->param[_mode][2]; // dir param

  /* cv mod? */
  if (_cv1)  { // division:
    _cv1 = (HALFSCALE - CV[_cv1]) >> 4;
    _div = limits(_p, 1, _cv1 + _div);
  }

  if (_cv2)  { // direction :
    _cv2 = CV[_cv2];
    if (_cv2 < THRESHOLD) _dir =  ~_dir & 1u;
  }

  /* clk counter: */
  _n   = _p->param[_mode][3];

  if (!_n) _out = 1;
  else _out = 0;

  _p->param[_mode][3]++;
  if (_n >= _div)  _p->param[_mode][3] = 0;

  if (_dir) _out = ~_out;  // invert?
  return _out & 1u;
}

// 4: euclid

uint8_t _euclid(struct params* _p) {

  uint8_t _mode, _n, _k, _offset, _out;
  int16_t _cv1, _cv2, _cv3;
  uint32_t _cnt;
  _mode = EUCLID;

  _cv1 = _p->cvmod[2];           // n CV
  _cv2 = _p->cvmod[3];           // k CV
  _cv3 = _p->cvmod[4];           // offset CV
  _n = _p->param[_mode][1];      // n param
  _k = _p->param[_mode][2];      // k param
  _offset = _p->param[_mode][3]; // _offset param
  _cnt = _p->cnt;                // clock count
  
  if (_cv1)  { // N
    _cv1 = (HALFSCALE - CV[_cv1]) >> 5;
    _n = limits(_p, 1, _cv1 + _n);
  }
  if (_cv2) { // K
    _cv2 = (HALFSCALE - CV[_cv2]) >> 5;
    _k = limits(_p, 2, _cv2 + _k);
  }
  if (_cv3) { // offset
    _cv3 = (HALFSCALE - CV[_cv3]) >> 5;
    _offset = limits(_p, 3, _cv3 + _offset);
  }

  if (_k >= _n ) _k = _n - 1;
  _out = ((_cnt + _offset) * _k) % _n;
  return (_out < _k) ? 1 : 0;
}

// 5: logic

uint8_t _logic(struct params* _p, uint8_t clk_state) {

  uint8_t _mode, _type, _op1, _op2, _out;
  int16_t _cv1, _cv2, _cv3;
  _mode = LOGIC;

  _cv1 = _p->cvmod[2];            // type CV
  _cv2 = _p->cvmod[3];            // op1 CV
  _cv3 = _p->cvmod[4];            // op2 CV
  _type  = _p->param[_mode][1];   // type param
  _op1   = _p->param[_mode][2] - 1; // op1 param
  _op2   = _p->param[_mode][3] - 1; // op2 param

  /* cv mod ?*/
  if (_cv1) {
    _cv1 = (HALFSCALE - CV[_cv1]) >> 6;
    _type = limits(_p, 1, _cv1 + _type);
  }
  if (_cv2) {
    _cv2 = (HALFSCALE - CV[_cv2]) >> 6;
    _op1 = limits(_p, 2, _cv2 + _op1) - 1;
  }
  if (_cv3) {
    _cv3 = (HALFSCALE - CV[_cv3]) >> 6;
    _op2 = limits(_p, 3, _cv3 + _op2) - 1;
  }

  _op1 = (clk_state >> _op1) & 1u;
  _op2 = (clk_state >> _op2) & 1u;

  switch (_type) {

    case 0: {  // AND
        _out = _op1 & _op2;
        break;
      }
    case 1: {  // OR
        _out = _op1 | _op2;
        break;
      }
    case 2: {  // XOR
        _out = _op1 ^ _op2;
        break;
      }
    case 3: {  // NAND
        _out = ~(_op1 & _op2);
        break;
      }
    case 4: {  // NOR
        _out = ~(_op1 | _op2);
        break;
      }
    default: {
        _out = 1;
        break;
      }
  }

  return _out & 1u;
}

// 6. DAC

uint8_t _dac(struct params* _p) {

  return 1;
}

/* ------------------------------------------------------------------   */

void outputDAC(struct params* _p, uint8_t clk_state) {

  int8_t _type, _scale, _polar, _mode;
  int16_t _cv1, _cv2, _cv3;
  _mode = DAC;

  _cv1   = _p->cvmod[1];        // type CV
  _cv2   = _p->cvmod[2];        // scale CV
  _cv3   = _p->cvmod[3];        // polar.CV
  _type  = _p->param[_mode][0]; // type param
  _scale = _p->param[_mode][1]; // scale param
  _polar = _p->param[_mode][2]; // polar param

  if (_cv1) {
    _cv1 = CV[_cv1];
    if (_cv1 < THRESHOLD) _type =  ~_type & 1u;
  }
  if (_cv2) {
    _cv2 = (HALFSCALE - CV[_cv2]) >> 5;
    _scale = limits(_p, 1, _scale + _cv2);
  }
  if (_cv3) {
    _cv3 = CV[_cv3];
    if (_cv3 < THRESHOLD) _polar =  ~_polar & 1u;
  }

  if (_type) DAC_OUT = random(132 * (_scale + 1)) +  _ZERO[_polar]; // RND
  else       DAC_OUT = _binary(clk_state, _scale + 1, _polar); // BIN
}

uint16_t _binary(uint8_t state, uint8_t _scale, uint8_t _pol) {

  uint16_t tmp, _state = state;

  tmp  = (_state & 1u) << 10;       // ch 1
  tmp += ((_state >> 1) & 1u) << 9; // ch 2
  tmp += ((_state >> 2) & 1u) << 8; // ch 3
  tmp += ((_state >> 4) & 1u) << 7; // ch 5
  tmp += ((_state >> 5) & 1u) << 6; // ch 6
  tmp = _scale * (tmp >> 5);
  
  return tmp*(_pol + 0x01) + _ZERO[_pol]; // uni/bi + offset
}

/* ------------------------------------------------------------------   */

void update_pw(struct params* _p) {

  int16_t _cv, _pw, _mode;
  _mode = _p->mode;
  _cv = _p->cvmod[1];                  // pw CV
  _pw = _p->param[_mode][PULSE_WIDTH]; // manual pw

  if (_cv) {                           // limit pulse_width
    _cv = (HALFSCALE - CV[_cv]) >> 3;
    _p->_pw = limits(_p, PULSE_WIDTH, _cv + _pw);
  }
  else _p->_pw = _pw;
}

/* ------------------------------------------------------------------   */

uint8_t limits(struct params* _p, uint8_t _param, int16_t _CV) {

  int16_t  _param_val = _CV;
  uint16_t _min = _p->param_min[_param];
  uint16_t _max = _p->param_max[_param];

  if (_param_val < _min) _param_val = _min;
  else if (_param_val > _max) _param_val = _max;
  else _param_val = _param_val;
  return _param_val;
}

/* ------------------------------------------------------------------   */

void clocksoff() {

  if (display_clock) {
    
      uint32_t _timestamp, _now, _pw, _prev_state, _state;
      
      _now = millis();
      _prev_state = display_clock;
    
      
      for (int i  = 0; i < CHANNELS; i++) {
        
            _pw = allChannels[i]._pw;
            _timestamp = allChannels[i].timestamp;
            _state =  allChannels[i]._state;
            
            if (_state && _now - _timestamp > _pw)  { 
          
              display_clock &= ~(1 << i);
              allChannels[i]._state = 0;
            }
     
      }

      if (_prev_state != display_clock) {

            /* turn clocks off */
            digitalWriteFast(CLK1, display_clock & 0x1);
            digitalWriteFast(CLK2, display_clock & 0x2);
            digitalWriteFast(CLK3, display_clock & 0x4);
            digitalWriteFast(CLK5, display_clock & 0x10);
            digitalWriteFast(CLK6, display_clock & 0x20);
            // DAC needs special treatment :
            if (!(display_clock & 0x8) && allChannels[DAC_CHANNEL].mode < DAC) analogWrite(A14, _ZERO[0]);
            else if (allChannels[DAC_CHANNEL].mode == DAC) display_clock |=  (1 << DAC_CHANNEL);
            
            MENU_REDRAW = 1;
      }
 }
}

/* ------------------------------------------------------------------   */



