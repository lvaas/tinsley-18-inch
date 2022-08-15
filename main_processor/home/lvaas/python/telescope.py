# Main supervisor for LVAAS Tinsley 18 inch Cassegrain system (Schlegel-McHugh Observatory)
#
# R. Hogg   2013-07
#
# Note: with the exception of the debug code, this is written to try to avoid
# allocating objects.  Since it must provide near-real-time performance, but
# only for an operating session of at most a few hours, garbage collection is
# disabled.

if True: # imports
    import evdev
    from evdev import InputDevice, categorize, ecodes
    from Lib.DS3231 import DS3231
    import math
    from optparse import OptionParser
    from select import select
    import RPi.GPIO as GPIO
    import smbus
    import socket
    import subprocess
    import gc
    import sys
    import time

if True: # global variables and initializatidebian custom servicedebian custom serviceon
    gc.disable # prevent unpredictable delays from garbage collection
    timestamp = 0.0 # output of time.time() for each loop
    my_subprocess = None

class debug(object): # class not instantiated
    enabled = False
    
    # constants
    relay_debug_symbols = 'nsSBewiolr'
    
    # reportable settings
    ra_switch = 0
    ra_rate = 0
    special_message = ''
    
    # static variables
    prev_message = ''
    
    @classmethod
    def init(cls, enabled):
        cls.enabled = enabled
        if not cls.enabled:
            return
        #=== automate this?
        cls.key_type_momentary = 0
        cls.key_type_modal = 1
        cls.key_type_message = 2
        cls.pad_keys = {
            ecodes.KEY_N:     (0,  cls.key_type_momentary),
            ecodes.KEY_S:     (1,  cls.key_type_momentary),
            ecodes.KEY_E:     (2,  cls.key_type_momentary),
            ecodes.KEY_W:     (3,  cls.key_type_momentary),
            ecodes.KEY_I:     (4,  cls.key_type_momentary),
            ecodes.KEY_O:     (5,  cls.key_type_momentary),
            ecodes.KEY_L:     (6,  cls.key_type_momentary),
            ecodes.KEY_R:     (7,  cls.key_type_momentary),
            ecodes.KEY_B:     (8,  cls.key_type_modal),
            ecodes.KEY_F:     (8,  cls.key_type_modal),
            ecodes.KEY_T:     (9,  cls.key_type_modal),
            ecodes.KEY_G:     (9,  cls.key_type_modal),
            ecodes.KEY_X:     (9,  cls.key_type_modal),
            ecodes.KEY_H:     (10, cls.key_type_modal),
            ecodes.KEY_D:     (10, cls.key_type_modal),
            ecodes.KEY_Z:     (10, cls.key_type_modal),
            ecodes.KEY_U:     (11, cls.key_type_momentary),
            ecodes.KEY_J:     (12, cls.key_type_momentary),
            ecodes.KEY_K:     (13, cls.key_type_momentary),
            ecodes.KEY_M:     (14, cls.key_type_momentary),
            ecodes.KEY_V:     (-1, cls.key_type_message),
            ecodes.KEY_0:     (-1, cls.key_type_message),
            ecodes.KEY_1:     (-1, cls.key_type_message),
            ecodes.KEY_2:     (-1, cls.key_type_message),
            ecodes.KEY_3:     (-1, cls.key_type_message),
            ecodes.KEY_4:     (-1, cls.key_type_message),
            ecodes.KEY_5:     (-1, cls.key_type_message),
            ecodes.KEY_6:     (-1, cls.key_type_message),
            ecodes.KEY_7:     (-1, cls.key_type_message),
            ecodes.KEY_8:     (-1, cls.key_type_message),
            ecodes.KEY_9:     (-1, cls.key_type_message),
            ecodes.KEY_ENTER: (-1, cls.key_type_message),
        }
        cls.num_key_slots = 15
        cls.key_output = cls.num_key_slots * ['-']
        cls.key_message = cls.num_key_slots * '-'
        cls.default_pad_message = '     '
        cls.pad_message = cls.default_pad_message
        cls.relay_message = len(cls.relay_debug_symbols) * '-'
    
    @classmethod
    def update_key_state(cls, key):
        if cls.enabled and (key.code in cls.pad_keys):
            index, key_type = cls.pad_keys[key.code]
            if key.state == 0:
                if key_type == cls.key_type_momentary:
                    cls.key_output[index] = '-'
            elif key_type != cls.key_type_message:
                cls.key_output[index] = key.ascii
            cls.key_message = ''.join(cls.key_output)
    
    @classmethod
    def show_pad_message(cls, message_buf):
        if cls.enabled:
            cls.pad_message = ''.join(message_buf)
    
    @classmethod
    def update_relays(cls):
        if cls.enabled:
            cls.relay_message = ''.join([cls.relay_debug_symbols[i] if r.state else '-' for i, r in enumerate(Relay.by_index)])
    
    @classmethod
    def message(cls):
        msg = 'pad %(key_message)s+"%(pad_message)s" relays %(relay_message)s sw %(ra_switch)d rate %(ra_rate)4d %(special_message)s\n' % cls.__dict__
        cls.pad_message = cls.default_pad_message
        cls.special_message = ''
        return msg
    
    @classmethod
    def update(cls):
        if cls.enabled:
            message = cls.message()
            if cls.prev_message != message:
                cls.prev_message = message
                sys.stdout.write(message)

class Key(object): # A key (including modal switch or virtual key) on the keypad
    by_code = dict()
    by_name = dict()
    
    def __init__(self, name, ascii, code):
        self.name = name
        Key.by_name[name] = self
        self.ascii = ascii
        self.code = code
        Key.by_code[code] = self
        self.value = 0
        self.state = 0

class background_sound_easter_egg(object): # class not instantiated
    value = False
    subprocess = None
    
    @classmethod
    def check(cls):
        if pad.modes[pad.mode_index_dec] != pad.mode_dec_rev:
            return
        if pad.modes[pad.mode_index_nav] != pad.mode_nav_off:
            return
        if pad.modes[pad.mode_index_light] != pad.mode_light_lo:
            return
        cls.value = not cls.value
        print cls.value
    
    @classmethod
    def update(cls):
        if cls.subprocess is not None:
            if cls.subprocess.poll() is not None:
                cls.subprocess = None
        if cls.value and (cls.subprocess is None):
            cls.subprocess = subprocess.Popen(['/usr/bin/aplay', '-q', '/home/lvaas/sound/background.wav'])


class ra_service_easter_egg(object): # class not instantiated
    value = False
    
    @classmethod
    def check(cls):
        if pad.modes[pad.mode_index_nav] != pad.mode_nav_off:
            return
        cls.value = not cls.value

class MomentaryKey(Key):
    def __init__(self, name, ascii, code, opposes=None, easter_egg=None):
        Key.__init__(self, name, ascii, code)
        self.opposing = None
        if opposes:
            other_key = Key.by_name[opposes]
            self.opposing = other_key
            other_key.opposing = self
        self.easter_egg = easter_egg
    
    def event(self, value):
        if self.easter_egg:
            if value and not self.value:
                self.easter_egg.check()
        self.value = value
        if self.opposing is not None:
            if value == 1 and self.opposing.value == 1:
                self.state = 0
                self.opposing.state = 0
            else:
                self.state = value
                self.opposing.state = self.opposing.value
        else:
            self.state = value

class ModalKey(Key):
    def __init__(self, name, ascii, code, modes, mode_index, mode_setting):
        Key.__init__(self, name, ascii, code)
        self.modes = modes
        self.mode_index = mode_index
        self.mode_setting = mode_setting
        self.state = 0
    
    def event(self, event_value):
        if event_value == 1:
            self.modes[self.mode_index] = self.mode_setting
        self.state = event_value

class pad_message(object): # class not instantiated
    len = 5
    buf = [None] * len
    index = 0
    value = 512 # intermediate value
    
    @classmethod
    def decode(cls):
        if cls.buf[0] != 'V':
            return
        value = 0
        for i in range(1, 5):
            c = cls.buf[i]
            if c < '0' or c > '9':
                return
            value = value * 10 + ord(c) - ord('0')
        cls.value = value

class MessageKey(Key):
    def __init__(self, name, ascii, code, start=False, term=False):
        Key.__init__(self, name, ascii, code)
        self.start = start
        self.term = term
        
    def event(self, event_value):
        if event_value == 1:
            if self.start:
                pad_message.index = 0
            if self.term:
                debug.show_pad_message(pad_message.buf)
                if pad_message.index == pad_message.len: # valid message
                    pad_message.decode()
                pad_message.index = pad_message.len + 1 # DQ message unless start received
                return
            if pad_message.index < pad_message.len:
                pad_message.buf[pad_message.index] = self.ascii
                pad_message.index += 1

class dispatcher(object): # class is not instantiated
    read_files = list()
    write_files = []
    excpt_files = []
    server_dict = dict()
    poll_interval = 0.1
    
    @classmethod
    def add_fileno(cls, fileno, server): # allowing for a little bit of memory leakage here
        cls.read_files.append(fileno)
        cls.server_dict[fileno] = server
    
    @classmethod
    def remove_fileno(cls, fileno): # allowing for a little bit of memory leakage here
        cls.read_files.remove(fileno)
        del cls.server_dict[fileno]
    
    @classmethod
    def update(cls):
        global timestamp
        r, w, x = select(cls.read_files, cls.write_files, cls.excpt_files, cls.poll_interval)
        timestamp = time.time()
        for fileno in r:
            cls.server_dict[fileno].service(fileno)

class pad(object): # not instantiated
    @classmethod
    def init(cls):
        cls.open = False
        
        if True: # definition of modes for modal keys
            cls.mode_index_dec = 0
            cls.mode_index_nav = 1
            cls.mode_index_light = 2
            
            cls.mode_dec_fwd = 0
            cls.mode_dec_rev = 1
            
            cls.mode_nav_off = 0
            cls.mode_nav_set = 1
            cls.mode_nav_guide = 2
            
            cls.mode_light_off = 0
            cls.mode_light_lo = 1
            cls.mode_light_hi = 2
            
            # array must agree with above constant values
            cls.modes = [cls.mode_dec_fwd, cls.mode_nav_off, cls.mode_light_off]
        
        if True: # define keys
            MomentaryKey('North',        'N',  ecodes.KEY_N)
            MomentaryKey('South',        'S',  ecodes.KEY_S, opposes='North')
            MomentaryKey('East',         'E',  ecodes.KEY_E)
            MomentaryKey('West',         'W',  ecodes.KEY_W, opposes='East')
            MomentaryKey('secFocusIn',   'I',  ecodes.KEY_I)
            MomentaryKey('secFocusOut',  'O',  ecodes.KEY_O, opposes='secFocusIn')
            MomentaryKey('domeLeft',     'L',  ecodes.KEY_L)
            MomentaryKey('domeRight',    'R',  ecodes.KEY_R, opposes='domeLeft')
            MomentaryKey('NSEgg',        'U',  ecodes.KEY_U, easter_egg=background_sound_easter_egg)
            MomentaryKey('EWEgg',        'J',  ecodes.KEY_J, easter_egg=ra_service_easter_egg)
            MomentaryKey('IOEgg',        'K',  ecodes.KEY_K)
            MomentaryKey('RLEgg',        'M',  ecodes.KEY_M)
            ModalKey(    'decRev',       'B',  ecodes.KEY_B, cls.modes, cls.mode_index_dec,   cls.mode_dec_rev)
            ModalKey(    'devFwd',       'F',  ecodes.KEY_F, cls.modes, cls.mode_index_dec,   cls.mode_dec_fwd)
            ModalKey(    'navSet',       'T',  ecodes.KEY_T, cls.modes, cls.mode_index_nav,   cls.mode_nav_set)
            ModalKey(    'navGuide',     'G',  ecodes.KEY_G, cls.modes, cls.mode_index_nav,   cls.mode_nav_guide)
            ModalKey(    'navOff',       'X',  ecodes.KEY_X, cls.modes, cls.mode_index_nav,   cls.mode_nav_off)
            ModalKey(    'lightHi',      'H',  ecodes.KEY_H, cls.modes, cls.mode_index_light, cls.mode_light_hi)
            ModalKey(    'LightLo',      'D',  ecodes.KEY_D, cls.modes, cls.mode_index_light, cls.mode_light_lo)
            ModalKey(    'lightOff',     'Z',  ecodes.KEY_Z, cls.modes, cls.mode_index_light, cls.mode_light_off)
            MessageKey(  'raUpdate',     'V',  ecodes.KEY_V, start=True)
            MessageKey(  '0',            '0',  ecodes.KEY_0)
            MessageKey(  '1',            '1',  ecodes.KEY_1)
            MessageKey(  '2',            '2',  ecodes.KEY_2)
            MessageKey(  '3',            '3',  ecodes.KEY_3)
            MessageKey(  '4',            '4',  ecodes.KEY_4)
            MessageKey(  '5',            '5',  ecodes.KEY_5)
            MessageKey(  '6',            '6',  ecodes.KEY_6)
            MessageKey(  '7',            '7',  ecodes.KEY_7)
            MessageKey(  '8',            '8',  ecodes.KEY_8)
            MessageKey(  '9',            '9',  ecodes.KEY_9)
            MessageKey(  'enter',        '\n', ecodes.KEY_ENTER, term=True)
        
        cls.update()
    
    @classmethod
    def service(cls, fileno):
        if fileno == cls.fileno:
            if not evdev.util.is_device(cls.device.fn):
                debug.special_message = 'paddle unplugged'
                dispatcher.remove_fileno(fileno)
                cls.open = False
                return True
            try:
                for event in cls.device.read():
                    # print "got event", event
                    if event.type == ecodes.EV_KEY:
                        key = Key.by_code[event.code]
                        key.event(event.value)
                        debug.update_key_state(key)
                    else:
                        # receiving synchronization events and null events, ignore them all
                        pass
            except:
                debug.special_message = 'read exception'
            return True
        else:
            return False
    
    @classmethod
    def update(cls):
        if cls.open:
            return
        try:
            cls.device = InputDevice('/dev/input/event0')
            cls.device.grab()
            cls.device.repeat = (0, 0)
            cls.fileno = cls.device.fd
            dispatcher.add_fileno(cls.fileno, cls)
            cls.open = True
            debug.special_message = 'paddle detected'
        except:
            pass

class alamode_i2c(object): # class not instantiated
    i2c = smbus.SMBus(1)
    addr = 42
    
    @classmethod
    def send_command(cls, code, value):
        i = 0
        while i < 5:
            try:
                cls.i2c.write_i2c_block_data(cls.addr, ord(code), [(value >> 8) & 0xff, value & 0xff])
                return
            except: # seem to occasionally get I/O error
                try:
                    cls.i2c.close()
                except:
                    pass
                cls.i2c = smbus.SMBus(1)
                debug.special_message = 'i2c reopened'

class gpio(object): # class not instantiated
    throbber_pin = 18
    ra_switch_pins = [15, 16, 22]
    
    GPIO.setmode(GPIO.BOARD)
    GPIO.setwarnings(False)
    
    GPIO.setup(throbber_pin, GPIO.OUT)
    throbber = GPIO.PWM(throbber_pin, 400)
    throbber.start(0)
    for pin in ra_switch_pins:
        GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    
    @classmethod
    def read_ra_switch(cls):
        switch = 0
        for pin in cls.ra_switch_pins:
            switch = switch << 1 | (1 if GPIO.input(pin) else 0)
        return switch
    
    @classmethod
    def update_throbber(cls):
        phase = math.fmod(timestamp / 3.0, 1.0) # 3-second cycle
        illum = abs(phase - 0.5) * 2.0
        cls.throbber.ChangeDutyCycle(illum * 100.0)

class ra_tracking(object): # class not instantiated
    # arbitrary rate mode constants
    service = 0
    variable = -1
    
    # rate settings (microseconds for 120Hz, divided by 2 by driver electronics)
    sidereal = 8311
    king = 8314
    solar = 8333
    lunar = 8615
    # guide_east = 10526 - solar # -25Hz
    guide_east = 10408 - solar # -25Hz -- changed 2022-07-16 to stop tripping limit on inverter processor
    guide_west = 6897 - solar # +25Hz
    guide_flag = 16384 # tell motor controller we are guiding, suppresses ST4
    
    # switch inputs
    switch_pins = [15, 16, 22]
    
    # switch settings
    default_switch = 7 # service mode (unlabeled) or disconnected
    # change default to compensate for dirty selector switch
    rates = { # by switch setting
        0: service, # (not used by hardware)
        1: service, # (not used by hardware)
        2: sidereal,
        3: solar,
        4: king,
        5: variable,
        6: lunar,
        7: service,
    }
    rates = { # by switch setting
        0: sidereal, # (not used by hardware)
        1: sidereal, # (not used by hardware)
        2: sidereal,
        3: solar,
        4: king,
        5: variable,
        6: lunar,
        7: sidereal,
    }
    
    # operational constants
    settling_delay = 1.0 # seconds
    resend_delay = 5.0 # seconds
    
    # static variables
    prev_switch = default_switch
    switch = default_switch
    service_ok_time = 0.0
    prev_rate = sidereal
    resend_time = 0.0
    
    @classmethod
    def update(cls):
        switch = gpio.read_ra_switch()
        if cls.prev_switch != switch: # transition
            cls.prev_switch = switch
            cls.service_ok_time = timestamp + cls.settling_delay
        elif switch != cls.default_switch or timestamp >= cls.service_ok_time: # not between detents
            cls.switch = switch
            debug.ra_switch = switch
        rate = cls.rates[cls.switch]
        if rate == cls.service:
            if ra_service_easter_egg.value:
                rate = cls.variable
        if rate == cls.variable:
            rate = cls.sidereal - (pad_message.value - 512)
        if rate != cls.service:
            if (pad.modes[pad.mode_index_nav] == pad.mode_nav_guide) and Key.by_name['East'].state:
                rate += cls.guide_east
                rate |= cls.guide_flag
            elif (pad.modes[pad.mode_index_nav] == pad.mode_nav_guide) and Key.by_name['West'].state:
                rate += cls.guide_west
                rate |= cls.guide_flag
        debug.ra_rate = rate
        if cls.prev_rate == rate: # same as previous setting anyway
            if timestamp < cls.resend_time:
                return
        cls.prev_rate = rate
        alamode_i2c.send_command('R', rate)
        cls.resend_time = timestamp + cls.resend_delay

class Relay(object):
    count = 0
    by_name = dict()
    by_index = list()
    def __init__(self, name):
        self.name = name
        self.index = Relay.count
        Relay.count += 1
        self.state = 0
        Relay.by_name[self.name] = self
        Relay.by_index.append(self)

class relays(object): # class not instantiated
    if True: # define relays
        # must be in this order to correspond to physical wiring!
        Relay('north')
        Relay('south')
        Relay('dec_set')
        Relay('ra_bias')
        Relay('east_set')
        Relay('west_set')
        Relay('sec_in')
        Relay('sec_out')
        Relay('dome_left')
        Relay('dome_right')
    prev_binary = 0
    
    @classmethod
    def update(cls):
        Relay.by_name['north'].state = Key.by_name['North'].state and pad.modes[pad.mode_index_nav] != pad.mode_nav_off
        Relay.by_name['south'].state = Key.by_name['South'].state and pad.modes[pad.mode_index_nav] != pad.mode_nav_off
        if pad.modes[pad.mode_index_dec] == pad.mode_dec_rev:
            t = Relay.by_name['north'].state
            Relay.by_name['north'].state = Relay.by_name['south'].state
            Relay.by_name['south'].state = t
        Relay.by_name['dec_set'].state = pad.modes[pad.mode_index_nav] == pad.mode_nav_set and (Relay.by_name['north'].state or Relay.by_name['south'].state)
        Relay.by_name['east_set'].state = (pad.modes[pad.mode_index_nav] == pad.mode_nav_set) and Key.by_name['East'].state
        Relay.by_name['west_set'].state = (pad.modes[pad.mode_index_nav] == pad.mode_nav_set) and Key.by_name['West'].state
        Relay.by_name['ra_bias'].state = Relay.by_name['east_set'].state == 0 and Relay.by_name['west_set'].state == 0
        Relay.by_name['sec_in'].state = Key.by_name['secFocusIn'].state
        Relay.by_name['sec_out'].state = Key.by_name['secFocusOut'].state
        Relay.by_name['dome_left'].state = Key.by_name['domeLeft'].state
        Relay.by_name['dome_right'].state = Key.by_name['domeRight'].state
        binary = 0
        mask = 1
        for relay in Relay.by_index:
            if relay.state:
                binary |= mask
            mask <<= 1
        if cls.prev_binary != binary:
            cls.prev_binary = binary
            alamode_i2c.send_command('F', binary)
        debug.update_relays()

class net(object): # class not instantiated
    @classmethod
    def init(cls):
        cls.host = ''
        cls.port = 4030 # default from SkySafari
        cls.backlog = 5
        cls.size = 1024
        cls.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM) 
        cls.server.bind((cls.host, cls.port))
        cls.server.listen(cls.backlog)
        cls.server_fileno = cls.server.fileno()
        cls.client_fileno = -1
        dispatcher.add_fileno(cls.server_fileno, cls)
    
    @classmethod
    def service(cls, fileno):
        if fileno == server_fileno:
            cls.client, cls.client_address = server.accept()
            cls.client_fileno = cls.client.fileno()
            selector.add_fileno(cls.client_fileno)
            return True
        elif fileno == cls.client_fileno:
            data = cls.client.recv(size)
            if data:
                pass
            dispatcher.remove_fileno(cls.client_fileno)
            cls.client.close()
            cls.client_fileno = -1
            return True
        else:
            return False

class options(object): # class is not instantiated
    parser = OptionParser()
    parser.add_option("-D", "--debug",
                      action="store_true", dest="debug", default=False,
                      help="print debug messages to stdout")
    (_options, args) = parser.parse_args()
    debug = _options.debug

if __name__ == '__main__':
    debug.init(options.debug)
    my_subprocess = subprocess.Popen(['/usr/bin/aplay', '-q', '/home/lvaas/sound/startup.wav'])
    pad.init()
    net.init()
    while True:
        if my_subprocess is not None:
            if my_subprocess.poll() is not None:
                my_subprocess = None
        dispatcher.update()
        ra_tracking.update()
        relays.update()
        gpio.update_throbber()
        pad.update() # in case unplugged and replugged
        background_sound_easter_egg.update()
        debug.update()
