
import av
import time 


rtmpurl = 'rtmp://ali.wangxiao.eaydu.com/live_bak/x_100_rtc_test'

input_ = av.open(rtmpurl,'r')


output = av.open('rtp://127.0.0.1:50766','w','rtp')



in_stream = input_.streams.video[0]
out_stream = output.add_stream(template=in_stream)


annexb_filter = av.BitStreamFilterContext('h264_mp4toannexb')


for packet in input_.demux(in_stream):
    
    if packet.dts is None:
        continue

    for out_packet in annexb_filter(packet):
        out_packet.stream = out_stream
        print(out_packet)
        output.mux(out_packet)


output.close()