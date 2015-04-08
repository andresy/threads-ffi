Threads
=======

The documentation for the _threads_ library is organized as follows
  * [Introduction](#intro)
  * [Installation](#install)
  * [Examples](#examples)
  * [Library](#library)

<a name="intro"/>
# Introduction #

Why another threading package for Lua, you might wonder? Well, to my
knowledge existing packages are quite limited: they create a new thread for
a new given task, and then end the thread when the task ends. The overhead
related to creating a new thread each time I want to parallelize a task
does not suit my needs. In general, it is also very hard to pass data
between threads.

The magic of the *threads* package lies in the seven following points:

  * Threads are created on demand (usually once in the program).
  * Jobs are submitted to the threading system in the form of a callback function. The job will be executed on the first free thread.
  * An ending callback will be executed in the main thread, when a job finishes.
  * Job callback are fully serialized (including upvalues!), which allows transparent copy of data to any thread.
  * Values returned by a job callback will be passed to the ending callback (serialized transparently).
  * As ending callbacks stay on the main thread, they can directly "play" with upvalues of the main thread.
  * Synchronization between threads is easy.

<a name="install"/>
# Installation #

At this time *threads* relies on two other packages: 

  * [Torch7](torch.ch) (for serialization) ; and
  * [SDL2](https://github.com/torch/sdl2-ffi/blob/master/README.md) for threads.

One could certainly port easily this package to other threading API
(pthreads, Windows threads...), but as SDL2 is really easy to install, and
very portable, I believe this dependency should not be a problem. If there
are enough requests, I might propose alternatives to SDL2 threads.

Torch is used for full serialization. One could easily get inspired from
Torch serialization system to adapt the package to its own needs. 
Torch should be straighforward to install, so this
dependency should be minor too.

At this time, if you have torch7 installed:
```sh
luarocks install https://raw.github.com/torch/sdl2-ffi/master/rocks/sdl2-scm-1.rockspec
luarocks install https://raw.github.com/torch/threads-ffi/master/rocks/threads-scm-1.rockspec
```

<a name="examples"/>
# Examples #

A  [simple example](example/simple.md) is better than convoluted explanations:

```lua
local Threads = require 'threads'
local sdl = require 'sdl2'

local nthread = 4
local njob = 10
local msg = "hello from a satellite thread"

-- init SDL (follow your needs)
sdl.init(0)

-- init the thread system
-- one lua state is created for each thread

-- the function takes several callbacks as input, which will be executed
-- sequentially on each newly created lua state
local threads = Threads(nthread,
   -- typically the first callback requires modules
   -- necessary to serialize other callbacks
   function()
      gsdl = require 'sdl2'
   end,

   -- other callbacks (one is enough in general!) prepare stuff
   -- you need to run your program
   function(idx)
      print('starting a new thread/state number:', idx)
      gmsg = msg -- we copy here an upvalue of the main thread
   end
)

-- now add jobs
local jobdone = 0
for i=1,njob do
   threads:addjob(
      -- the job callback
      function()
         local id = tonumber(gsdl.threadID())
         -- note that gmsg was intialized in last worker callback
         print(string.format('%s -- thread ID is %x', gmsg, id))

         -- return a value to the end callback
         return id
      end,

      -- the end callback runs in the main thread.
      -- takes output of the previous function as argument
      function(id)
         print(string.format("task %d finished (ran on thread ID %x)", i, id))

         -- note that we can manipulate upvalues of the main thread
         -- as this callback is ran in the main thread!
         jobdone = jobdone + 1 
      end
   )
end

-- wait for all jobs to finish
threads:synchronize()

print(string.format('%d jobs done', jobdone))

-- of course, one can run more jobs if necessary!

-- terminate threads
threads:terminate()
```

Typical output:

```sh
starting a new thread/state
starting a new thread/state
starting a new thread/state
starting a new thread/state
hello from a satellite thread -- thread ID is cd24000
hello from a satellite thread -- thread ID is cec8000
hello from a satellite thread -- thread ID is d06c000
hello from a satellite thread -- thread ID is cd24000
task 1 finished (ran on thread ID cd24000)
hello from a satellite thread -- thread ID is d210000
task 2 finished (ran on thread ID cec8000)
task 3 finished (ran on thread ID d06c000)
task 4 finished (ran on thread ID cd24000)
task 5 finished (ran on thread ID d210000)
hello from a satellite thread -- thread ID is cec8000
hello from a satellite thread -- thread ID is d06c000
hello from a satellite thread -- thread ID is cd24000
task 6 finished (ran on thread ID cec8000)
hello from a satellite thread -- thread ID is d210000
hello from a satellite thread -- thread ID is cec8000
task 7 finished (ran on thread ID d06c000)
task 8 finished (ran on thread ID cd24000)
task 9 finished (ran on thread ID d210000)
task 10 finished (ran on thread ID cec8000)
10 jobs done
```

## Advanced Example ##

See a neural network [threaded training example](benchmark/README.md) for a
more advanced usage of `threads`.

<a name="library"/>
# Library #

The library consists of two main classes and a serialization library:
  
  * [Threads](#threads.main) : a thread pool ;
  * [Worker](#worker) : a thread-safe task queue ; and
  * [serialize](#serialize) : functions for serialization and deserialization.

<a name='threads.main'/>
## Threads ##
This class is used to manage a set of worker threads. The class is 
returned upon requiring the package:

```lua
Threads = require 'threads'
```

Internally, a Threads instance uses two [Workers](#worker), 
i.e. thread-safe task queues:
  
  * `mainworker` is used by the worker threads to communicate serialized `endcallback` functions back to the main thread ; and
  * `threadworker` is used by the main thread to communicate serialized `callback` function to the worker threads.
  
It is important to note that there is only one `threadworker` such that there is no way of 
knowing which `callback` job will be executed by which worker thread. 
Internally, the worker threads consist of an infinite loop that waits for 
the next job to be available on the `threadworker` queue. When a job is 
available, one of the threads executes it and returns the results 
back to the main thread via the `mainworker` queue. Upon receipt of the 
results, an optional `endcallback` is executed on the main thread 
(see [Threads:addjob](#threads.addjob)).

Each thread has its own [lua_State](http://www.lua.org/pil/24.1.html).

<a name='threads'/>
### Threads(N,[f1,f2,...]) ###
Argument `N` of this constructor specifies the number of worker threads
that will be spawned. The optional arguments `f1,f2,...` can be a list 
of functions to execute in each worker thread. To be clear, all of 
these functions will be executed in each thread. However, each optional 
function `f` takes an argument `threadIdx` which is a number between 
`1` and `N` identifying each thread. This could be used to make each 
thread have different behaviour.

Example:
```lua
Threads(4,
   function(threadIdx)
      print("Initializing thread " .. threadIdx)
   end
)
```
<a name='threads.addjob'/>
### Threads:addjob(callback, [endcallback, ...]) ###
This method is used to queue jobs to be executed by the pool of worker threads.
The `callback` is a function that will be executed in each worker thread 
with the optional `...` arguments. 
The `endcallback` is a function that will be executed in the main thread
(the one calling this method). It defaults to `function() end`. 

Unlike [addjobasync](#threads.addjobasync), this method will internally 
call [dojob](#threads.dojob) until the `threadworker` 
[Worker](#worker) is ready to accept more jobs (see [acceptsjobs](#threads.acceptsjobs)).
This is to prevent `addjob` to be called when both the `threadworker` and 
`mainworker` are full, such that adding a job to the `threadworker` would
cause a deadlock (i.e. the function call would block indefinitely).

Before being executed in the worker thread, the `callback` and its 
optional `...` arguments are serialized 
by the main thread and unserialized by the worker. Other than through 
the optional arguments, the main thread can also transfer data to 
the worker by using upvalues:
```lua
local upvalue = 10
threads:addjob(
   function() 
      workervalue = upvalue
      return 1
   end,
   function(inc)
      upvalue = upvalue + inc
   end
)
```
In the above example, each worker thread will have a global variable `workervalue` 
which will contain a copy of the main thread's `upvalue`. Note that 
if the main thread's upvalue were global, as opposed to `local` 
it would not be an upvalue, and therefore would not be serialized along
with the `callback`. In which case, `workervalue` would be `nil`. 

In the same example, the worker also communicates a value to the main thread. 
This is accomplished by having the `callback` return one ore many values which 
will be serialized and unserialized as arguments to the `endcallback` function. 
In this case a value of `1` is received by the main thread as argument `inc` to the 
`endcallback` function, which then uses it to increment `upvalue`. This
demonstrates how communication between threads is easily achieved using 
the `addjob` method. 

<a name='threads.addjobasync'/>
### Threads:addjobasync(callback, [endcallback, ...]) ###
This method is the asynchronous version of [addjob](#threads.addjob).
However, unlike `addjob`, this method _will not_ internally 
call [dojob](#threads.dojob) when `threadworker` is full.
To prevent deadlocks, calls to `addjobasync` can be preceded by a call 
to [acceptsjobs](#threads.acceptsjobs) to verify that the `threadworker`
can still accept jobs.

Here is can example of how this method can be used:
```lua
local function job()
   -- stuff to do in a thread
end

local result -- upvalue
local function endcallback(res)
   result = res
end

nPut = 0
nGet = 0
maxJobs = 77 -- some arbitrary predefined number of jobs to execute
while nGet < maxJobs do
   while nPut < maxJobs and thread:acceptsJob() do -- fill the queue as much as can be
     thread:asyncaddjob(job, endcallback) -- async version (doesn't call dojob)
     nPut = nPut + 1
   end
   if threads:hasjob() then -- make sure that there is something to do
      threads:dojob() -- endcallback is executed and fill result ; result is ready!
      nGet = nGet + 1
      if threads:haserror() then -- note that dojob() does not (and will not) check for errors
         threads:synchronize() -- finishes everything and throws error
      end
      dosomethingwith(result) -- do something in main (this) thread with result
   end
end
```

<a name='threads.dojob'/>
### Threads:dojob() ###
This method is used to tell the main thread to execute the next 
`endcallback` in the queue (see [Threads:addjob](#threads.addjob)). If 
no such job is available, the main thread of execution will wait (i.e. block)
until the `mainthread` Worker (i.e. queue) is filled with a job. 
Therefore, it is important that every call to `dojob` be preceded by 
a call to `addjob`.

<a name='threads.acceptsjobs'/>
### Threads:acceptsjobs() ###
This method returns true when the Threads pool can still accept jobs, 
i.e. if the `threadworker` [Worker](#worker) (the thread-safe queue 
of tasks to be executed by threads in the pool) isn't full.

<a name='threads.hasjob'/>
### Threads:hasjob() ###
Returns true if there are any jobs in the Threads pool that haven't yet 
had their `endcallback` executed via a call to [dojob](#threads.dojob).

<a name='threads.synchronize'/>
### Threads:synchronize() ###
This method will call [dojob](#threads.dojob) until all `callbacks` 
and corresponding `endcallbacks` are executed on the worker and main 
threads, respectively. This method will also raise an error for any 
errors raised in the pool of worker threads.

<a name='threads.terminate'/>
### Threads:terminate() ###
This method will call [synchronize](#threads.synchronize), 
terminate each worker and free their memory.

<a name='worker'/>
## Worker ##
This class is in effect a thread-safe task queue. The class is 
returned upon requiring the sub-package:

```lua
Worker = require 'threads.worker'
```

### Worker(N) ###
The Worker constructor takes a single argument `N` which specifies the 
maximum size of the queue. 

<a name='worker.addjob'/>
### Worker:addjob(callback, [...]) ###
This method is called by a thread to *put* a job in the queue. 
The job is specified in the form of a `callback` function taking arguments `...`. 
Both the `callback` function and `...` arguments are serialized before being 
*put* into the queue.  If the queue is full, i.e. it has more than 
`N` jobs, the calling thread will wait (i.e. block) until a job is retrieved
by another thread.

<a name='worker.dojob'/>
### [res] Worker:dojob() ###
This method is called by a thread to *get*, unserialize and execute a job inserted 
via [addjob](#worker.addjob) from the queue. A calling thread will wait 
(i.e. block) until a new job can be retrieved. It returns to the calller 
whatever the job function returns after execution.

<a name='serialize'/>
## Serialize ##
A table of serialization functions is returned upon requiring the sub-package:

```lua
serialize = require 'threads.serialize'
```

<a name='serialize.save'/>
### [code_p, sz] serialize.save(func) ###
This function serializes function `func`. It returns 

  * `code_p` : a constant ffi pointer to a C `char` array ; and
  * `sz` : the size of this array.
  
<a name='serialize.load'/>
### [obj] serialize.load(code_p, sz) ###
This function unserializes the outputs of a [serialize.save](#serialize.save).
The unserialized object `obj` is returned.

<a name='sharedserialize'/>
### Shared Serialize ###

Often times, we need to communicate [torch.Tensors](https://github.com/torch/torch7/blob/master/doc/tensor.md) 
between threads without actually serializing the underlying data. Instead we 
usually just serialize/deserialize the pointer to the data and the Tensor metadata, 
which is much more efficient. 

For convenience, the _threads_ library provides an alternative sub-package
with the same API as `threads.serialize`, which automatically incorporates
this behavior:

```lua
serialize = require 'threads.sharedserialize'
```

In effect this adds transparent Tensor sharing between threads.
Note that Tensor reference counts are_not thread safe (yet), 
so you have to know what you are doing when using this option. For example,
make sure different threads don't try to read or write to a Tensor 
shared by both at the same time.
