local sys = require 'sys'
local t = require 'threads'

-- Example 1. Two child threads send even and odd numbers
-- The main thread collates the results
local odd = t.Channel(1)
local even = t.Channel(1)

local pool = t.Threads(2)

pool:addjob(function()
  for i=1,100,2 do
    odd:send(i)
  end
end)

pool:addjob(function()
  for i=2,100,2 do
    even:send(i)
  end
end)

for i=1,100 do
  local v
  if i % 2 == 1 then
    v = odd:receive()
  else
    v = even:receive()
  end
  assert(v == i)
end

pool:synchronize()

-- Example 2. Close should immediately cause the channel to return
local ch = t.Channel(1)
pool:addjob(function()
  local v, ok = ch:receive()
  assert(v == nil and ok == false)
end)

pool:addjob(function()
  local v, ok = ch:receive()
  assert(v == nil and ok == false)
end)

sys.sleep(0.01)
ch:close()
pool:synchronize()


-- Example 3. Immedate sends and receives
local ch = t.Channel(2)
assert(ch:isend('a') == true)
assert(ch:isend('b') == true)
assert(ch:isend('c') == false)
local v, ok = ch:ireceive()
assert(v == 'a' and ok == true)
v, ok = ch:ireceive()
assert(v == 'b' and ok == true)
v, ok = ch:ireceive()
assert(v == nil and ok == false)

print('OK')
