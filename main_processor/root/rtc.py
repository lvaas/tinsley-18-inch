from Lib.DS3231 import DS3231
from Lib.Adafruit_I2C import Adafruit_I2C
from optparse import OptionParser

# import time
import datetime

BAUD = 9600
gui = False
    
def checkRTC():
    rtc = DS3231()
    date = range(7);
    rtc.ds3231.writeList(0, date);
    date[6] = 0;
    date1 = rtc.getTime()
    if date1.year != 2006:
        out = False
    else:
        now = datetime.datetime.now()
        rtc.setTime(now)
        date2 = rtc.getTime()
        out = now.year == date2.year
        print 'checkRCT() date:', date2
    return out

# print checkRTC()

if __name__ == '__main__':
    parser = OptionParser()
    parser.add_option("--set-rtc", dest="set_rtc", default=None,
                  help="set real-time clock to DATETIME", metavar="DATETIME")
    parser.add_option("--raw",
                  action="store_true", dest="raw", default=False,
                  help="output ISO format date/time only")
    
    (options, args) = parser.parse_args()
    
    rtc = DS3231()
    if options.set_rtc is not None:
        set_rtc = datetime.datetime.strptime(options.set_rtc, '%Y-%m-%dT%H:%M:%S')
        print 'setting real-time clock to', set_rtc
        rtc.setTime(set_rtc)
    elif options.raw:
        print rtc.getTime()
    else:
        print 'real-time clock setting is', rtc.getTime()
        print 'system time is', datetime.datetime.now()
