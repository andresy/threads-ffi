require 'torch'
local clib = require 'libthreads'
local Threads = require 'threads.threads'

local M = {}
local Channel = torch.class('threads.Channel', M)

function Channel:__init(n)
   assert(n >= 0, 'invalid size: ' .. tostring(n))
   self.__channel = clib.channel(n)
end

function Channel:send(value)
   local serialize = require(Threads.__serialize)
   self.__channel:send(serialize.save(value), false)
end

function Channel:isend(value)
   local serialize = require(Threads.__serialize)
   return self.__channel:send(serialize.save(value), true)
end

function Channel:values()
   return function()
      return self:receive()
   end
end

function Channel:receive()
   local serialize = require(Threads.__serialize)
   local value, ok = self.__channel:receive(false)
   if ok then
      value = serialize.load(value)
   end
   return value, ok
end

function Channel:ireceive()
   local serialize = require(Threads.__serialize)
   local value, ok = self.__channel:receive(true)
   if ok then
      value = serialize.load(value)
   end
   return value, ok
end

function Channel:close()
   self.__channel:close()
end

function Channel:__write(f)
   assert(torch.type(f) == 'torch.MemoryFile', 'can only serialize to shared memory')
   clib.channel.retain(self.__channel)
   f:writeDouble(self.__channel:id())
end

function Channel:__read(f)
   assert(torch.type(f) == 'torch.MemoryFile', 'can only serialize to shared memory')
   local id = f:readDouble()
   self.__channel = clib.channel.fromid(id)
end

return M.Channel