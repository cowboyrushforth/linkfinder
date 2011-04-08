require 'rubygems'
require 'ffi-rzmq'

if ARGV.size != 1
  puts "Usage:\n\truby client.rb <link>"
  exit
else
  link = ARGV[0]
end

# setup context
context = ZMQ::Context.new(1)

# socket to talk to server
requester = context.socket(ZMQ::REQ)

# connect
puts "connecting"
requester.connect("tcp://localhost:5555")

puts "sending: #{link}"

requester.send_string link

reply = requester.recv_string
puts "Received reply:"
puts "---------"
puts "#{reply}"
puts "---------"

puts requester.close
