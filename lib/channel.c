#include "TH.h" /* for THCharStorage */
#include "luaT.h" /* for handling THCHarStorage */
#include "luaTHRD.h"
#include "THThread.h"
#include <lua.h>
#include <lualib.h>


typedef struct THChannel_ {
  THMutex *mutex;
  THCondition *notfull;
  THCondition *notempty;
  THCharStorage **args;

  unsigned int head;
  unsigned int tail;
  unsigned int size;
  int isempty;
  int isfull;
  int isclosed;
  int refcount;
} THChannel;

static int channel_new(lua_State *L)
{
  int size = luaL_checkinteger(L, 1);
  if (size < 0) luaL_error(L, "threads: invalid channel size");

  THChannel* channel = (THChannel*) calloc(1, sizeof(THChannel));
  if (!channel) luaL_error(L, "threads: channel new out of memory");

  channel->mutex = THMutex_new();
  channel->notfull = THCondition_new();
  channel->notempty = THCondition_new();
  channel->args = calloc(size, sizeof(THCharStorage*));
  channel->isempty = 1;
  channel->isfull = 0;
  channel->refcount = 1;
  channel->size = size;

  if(!channel->mutex || !channel->notfull || !channel->notempty ||
     !channel->args) {
    goto error;
  }

  if(!luaTHRD_pushudata(L, channel, "threads.channel")) {
    goto error;
  }

  return 1;

error:
  THMutex_free(channel->mutex);
  THCondition_free(channel->notfull);
  THCondition_free(channel->notempty);
  free(channel->args);
  free(channel);
  luaL_error(L, "threads: channel new out of memory");
  return 0;
}

static int channel_fromid(lua_State *L)
{
  long id = luaL_checknumber(L, 1);
  THChannel* channel = (THChannel*) id;
  if (luaTHRD_pushudata(L, channel, "threads.channel")) {
    THAtomicIncrementRef(&channel->refcount);
    return 1;
  }
  luaL_error(L, "threads: channel fromid out of memory");
  return 0;
}

static int channel_free(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");
  if(THAtomicDecrementRef(&channel->refcount))
  {
    int i;
    THMutex_free(channel->mutex);
    THCondition_free(channel->notfull);
    THCondition_free(channel->notempty);
    for(i = 0; i < channel->size; i++) {
      if(channel->args[i])
        THCharStorage_free(channel->args[i]);
    }
    free(channel->args);
    free(channel);
  }
  return 0;
}

static int channel_retain(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");
  THAtomicIncrementRef(&channel->refcount);
  return 0;
}

static int channel_send(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");
  THCharStorage *storage = luaT_checkudata(L, 2, "torch.CharStorage");
  int immediate = lua_toboolean(L, 3);

  THMutex_lock(channel->mutex);
  while (channel->isfull && !channel->isclosed) {
    if (immediate) {
      lua_pushboolean(L, 0);
      THMutex_unlock(channel->mutex);
      return 1;
    }

    THCondition_wait(channel->notfull, channel->mutex);
  }

  if (channel->isclosed) {
    THMutex_unlock(channel->mutex);
    luaL_error(L, "threads: channel is closed");
    return 0;
  }

  THCharStorage_retain(storage);
  channel->args[channel->tail] = storage;
  channel->tail++;
  if (channel->tail >= channel->size) {
    channel->tail = 0;
  }
  channel->isfull = (channel->tail == channel->head);
  channel->isempty = 0;

  THMutex_unlock(channel->mutex);
  THCondition_signal(channel->notempty);

  lua_pushboolean(L, 1);
  return 1;
}

static int channel_receive(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");
  int immediate = lua_toboolean(L, 2);

  THMutex_lock(channel->mutex);
  while (channel->isempty) {
    if (immediate || channel->isclosed) {
      lua_pushnil(L);
      lua_pushboolean(L, 0);
      THMutex_unlock(channel->mutex);
      return 2;
    }

    THCondition_wait(channel->notempty, channel->mutex);
  }

  THCharStorage* storage = channel->args[channel->head];
  channel->args[channel->head] = NULL;
  channel->head++;
  if (channel->head >= channel->size) {
    channel->head = 0;
  }
  channel->isfull = 0;
  channel->isempty = (channel->tail == channel->head);

  THMutex_unlock(channel->mutex);
  THCondition_signal(channel->notfull);

  luaT_pushudata(L, storage, "torch.CharStorage");
  lua_pushboolean(L, 1);

  return 2;
}

static int channel_close(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");

  THMutex_lock(channel->mutex);
  channel->isclosed = 1;
  THMutex_unlock(channel->mutex);

  THCondition_broadcast(channel->notfull);
  THCondition_broadcast(channel->notempty);

  return 0;
}

static int channel_id(lua_State *L)
{
  THChannel *channel = luaTHRD_checkudata(L, 1, "threads.channel");
  lua_pushinteger(L, (long)channel);
  return 1;
}

static int channel__index(lua_State *L)
{
  luaTHRD_checkudata(L, 1, "threads.channel");
  lua_getmetatable(L, 1);
  if(lua_isstring(L, 2)) {
    lua_pushstring(L, "__get");
    lua_rawget(L, -2);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
  }
  lua_insert(L, -2);
  lua_rawget(L, -2);
  return 1;
}


static const struct luaL_Reg channel__ [] = {
  {"new", channel_new},
  {"fromid", channel_fromid},
  {"retain", channel_retain},
  {"free", channel_free},
  {"id", channel_id},
  {"__gc", channel_free},
  {"__index", channel__index},
  {NULL, NULL}
};

static const struct luaL_Reg channel_get__ [] = {
  {"send", channel_send},
  {"receive", channel_receive},
  {"close", channel_close},
  {"id", channel_id},
  {NULL, NULL}
};

static void channel_init_pkg(lua_State *L)
{
  if(!luaL_newmetatable(L, "threads.channel"))
    luaL_error(L, "threads: threads.channel type already exists");
  luaL_setfuncs(L, channel__, 0);

  lua_pushstring(L, "__get");
  lua_newtable(L);
  luaL_setfuncs(L, channel_get__, 0);
  lua_rawset(L, -3);

  lua_pop(L, 1);

  lua_pushstring(L, "channel");
  luaTHRD_pushctortable(L, channel_new, "threads.channel");
  lua_rawset(L, -3);
}
