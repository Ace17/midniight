// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// Game logic

#include <map>

#include "base/error.h"
#include "base/scene.h"
#include "base/util.h"
#include "base/view.h"

#include "entity_factory.h"
#include "game.h"
#include "load_quest.h"
#include "models.h" // MDL_TILES_00
#include "physics.h"
#include "player.h"
#include "quest.h"
#include "state_machine.h"
#include "toggle.h"
#include "variable.h"

using namespace std;

struct EntityConfigImpl : IEntityConfig
{
  string getString(const char* varName, string defaultValue) override
  {
    auto i = values.find(varName);

    if(i == values.end())
      return defaultValue;

    return i->second;
  }

  int getInt(const char* varName, int defaultValue) override
  {
    auto i = values.find(varName);

    if(i == values.end())
      return defaultValue;

    return atoi(i->second.c_str());
  }

  map<string, string> values;
};

static
void spawnEntities(Room const& room, IGame* game, int levelIdx)
{
  // avoid collisions between static entities from different rooms
  int id = levelIdx * 1000;

  for(auto& spawner : room.spawners)
  {
    EntityConfigImpl config;
    config.values = spawner.config;

    auto entity = createEntity(spawner.name, &config);
    entity->id = id;
    entity->pos = spawner.pos;
    game->spawn(entity.release());

    ++id;
  }
}

struct GameState : Scene, private IGame
{
  GameState(View* view) :
    m_view(view)
  {
    m_shouldLoadLevel = true;
    m_shouldLoadVars = true;
    m_quest = loadQuest("res/quest.gz");
  }

  ////////////////////////////////////////////////////////////////
  // Scene: Game, seen by the engine

  Scene* tick(Control c) override
  {
    if(startButton.toggle(c.start))
      return createPausedState(m_view, this, &m_quest, m_level);

    loadLevelIfNeeded();

    m_player->think(c);

    updateEntities();

    processEvents();
    updateCamera();

    updateDebugFlag(c.debug);

    if(m_gameFinished)
    {
      std::unique_ptr<Scene> deleteMeOnReturn(this);
      return createEndingState(m_view);
    }

    return this;
  }

  void draw() override
  {
    if(!m_player)
      return;

    sendActorsForTileMap();

    vector<Actor> actors;

    for(auto& entity : m_entities)
    {
      actors.clear();
      entity->addActors(actors);

      for(auto actor : actors)
        m_view->sendActor(actor);

      if(m_debug)
        m_view->sendActor(getDebugActor(entity.get()));
    }

    {
      Actor background = { Vector(-8, -8), MDL_BACKGROUND };
      background.scale = Size(16, 16);
      background.screenRefFrame = true;
      background.zOrder = -2;
      m_view->sendActor(background);
    }
  }

  ////////////////////////////////////////////////////////////////
  // internals

  void loadLevelIfNeeded()
  {
    if(m_shouldLoadLevel)
    {
      loadLevel(m_level);
      m_player->translate(m_transform);
      m_shouldLoadLevel = false;
      setAmbientLight(0);
    }
  }

  void updateDebugFlag(float debugFlag)
  {
    m_debug = debugFlag;
  }

  void updateEntities()
  {
    for(auto& e : m_entities)
      e->tick();

    m_physics->checkForOverlaps();
    removeDeadThings();
  }

  void processEvents()
  {
    auto events = move(m_eventQueue);

    for(auto& event : events)
    {
      if(auto levelBoundaryEvent = event->as<TouchLevelBoundary>())
        onTouchLevelBoundary(levelBoundaryEvent);
      else if(event->as<SaveEvent>())
        onSaveEvent();
      else if(event->as<FinishGameEvent>())
        m_gameFinished = true;
    }
  }

  void updateCamera()
  {
    auto cameraPos = m_player->pos;
    cameraPos.y += 1.5;

    // prevent camera from going outside the level
    auto const limit = 8.0f;
    cameraPos.x = ::clamp(cameraPos.x, limit, m_currRoom->size.width * 16.0f - limit);
    cameraPos.y = ::clamp(cameraPos.y, limit, m_currRoom->size.height * 16.0f - limit);

    m_view->setCameraPos(cameraPos);
  }

  void sendActorsForTileMap() const
  {
    auto const model = MDL_TILES_00 + m_theme % 8;

    auto onCell =
      [&] (int x, int y, int tile)
      {
        if(tile == -1)
          return;

        auto actor = Actor { Vector(x, y), model };
        actor.action = tile;
        actor.scale = UnitSize;
        actor.zOrder = -1;
        m_view->sendActor(actor);
      };

    m_tilesForDisplay->scan(onCell);
  }

  void removeDeadThings()
  {
    for(auto& entity : m_entities)
    {
      if(entity->dead)
      {
        entity->leave();
        m_physics->removeBody(entity.get());
      }
    }

    unstableRemove(m_entities, &isDead);

    for(auto& spawned : m_spawned)
    {
      spawned->game = this;
      spawned->physics = m_physics.get();
      spawned->enter();

      m_physics->addBody(spawned.get());
      m_entities.push_back(move(spawned));
    }

    m_spawned.clear();
  }

  static bool isDead(unique_ptr<Entity> const& e)
  {
    return e->dead;
  }

  void loadLevel(int levelIdx)
  {
    ///////////////////////////////////////////////////////////////////////////
    // destroy current game arena
    ///////////////////////////////////////////////////////////////////////////
    if(m_player)
    {
      for(auto& entity : m_entities)
        if(m_player == entity.get())
          entity.release();
    }

    m_physics.reset();

    m_entities.clear();
    m_spawned.clear();

    if(m_shouldLoadVars)
    {
      m_vars.clear();

      for(auto& savedVar : m_savedVars)
        getVariable(savedVar.first)->set(savedVar.second);

      m_shouldLoadVars = false;
    }

    ///////////////////////////////////////////////////////////////////////////
    // create new game arena
    ///////////////////////////////////////////////////////////////////////////

    m_physics = createPhysics();
    m_physics->setEdifice(bind(&GameState::isBoxSolid, this, placeholders::_1));

    if(levelIdx < 0 || levelIdx >= (int)m_quest.rooms.size())
      throw Error("No such level");

    m_currRoom = &m_quest.rooms[levelIdx];

    auto& level = m_quest.rooms[levelIdx];
    spawnEntities(level, this, levelIdx);
    m_tilesForDisplay = &level.tilesForDisplay;
    m_theme = level.theme;
    m_view->playMusic(level.theme);

    // load new background
    {
      char buffer[256];
      int n = sprintf(buffer, "res/sprites/background-%02d.model", level.theme);
      String path = buffer;
      path.len = n;
      m_view->preload({ ResourceType::Model, MDL_BACKGROUND, path });
    }

    if(!m_player)
    {
      EntityConfigImpl config;
      m_player = dynamic_cast<Player*>(createEntity("Hero", &config).release());
      m_player->pos = Vector(level.start.x, level.start.y);
      postEvent(make_unique<SaveEvent>());
    }

    spawn(m_player);
  }

  void onTouchLevelBoundary(const TouchLevelBoundary* event)
  {
    m_shouldLoadLevel = true;
    m_transform = event->transform;
    m_level = event->targetLevel;
  }

  int m_level = 1;
  int m_theme = 0;
  Vector m_transform;
  bool m_shouldLoadLevel = false;
  bool m_shouldLoadVars = false;

  map<int, unique_ptr<IVariable>> m_vars;
  vector<unique_ptr<Event>> m_eventQueue;

  ////////////////////////////////////////////////////////////////
  // IGame: game, as seen by the entities

  void playSound(SOUND sound) override
  {
    m_view->playSound(sound);
  }

  void stopMusic() override
  {
    m_view->stopMusic();
  }

  void spawn(Entity* e) override
  {
    m_spawned.push_back(unique(e));
  }

  IVariable* getVariable(int name) override
  {
    if(!m_vars[name])
      m_vars[name] = make_unique<Variable>();

    return m_vars[name].get();
  }

  void postEvent(unique_ptr<Event> event) override
  {
    m_eventQueue.push_back(move(event));
  }

  Vector getPlayerPosition() override
  {
    return m_player->pos;
  }

  int m_savedLevel = 0;
  Vector m_savedPos = NullVector;
  map<int, int> m_savedVars;

  void onSaveEvent()
  {
    m_savedLevel = m_level;
    m_savedPos = m_player->pos;
    m_savedVars.clear();

    for(auto& var : m_vars)
      m_savedVars[var.first] = var.second->get();
  }

  void respawn() override
  {
    printf("Respawning!\n");
    m_level = m_savedLevel;
    m_transform = m_savedPos - m_player->pos + Vector(0, 0.01);
    m_shouldLoadLevel = true;
    m_shouldLoadVars = true;
  }

  void textBox(char const* msg) override
  {
    m_view->textBox(msg);
  }

  void setAmbientLight(float light) override
  {
    m_view->setAmbientLight(light);
  }

  Quest m_quest;
  Room* m_currRoom = nullptr;
  Player* m_player = nullptr;
  View* const m_view;
  unique_ptr<IPhysics> m_physics;
  bool m_gameFinished = false;

  const Matrix2<int>* m_tilesForDisplay;
  bool m_debug;
  Toggle startButton;

  vector<unique_ptr<Entity>> m_entities;
  vector<unique_ptr<Entity>> m_spawned;

  bool isBoxSolid(IntBox box)
  {
    auto const x1 = box.pos.x;
    auto const y1 = box.pos.y;
    auto const x2 = box.pos.x + box.size.width;
    auto const y2 = box.pos.y + box.size.height;

    auto const col1 = x1 / PRECISION;
    auto const col2 = x2 / PRECISION;
    auto const row1 = y1 / PRECISION;
    auto const row2 = y2 / PRECISION;

    for(int row = row1; row <= row2; row++)
      for(int col = col1; col <= col2; col++)
        if(m_currRoom->tiles.isInside(col, row))
        {
          const auto tile = m_currRoom->tiles.get(col, row);

          if(tile != 0 && tile < 16)
            return true;
        }

    return false;
  }

  // static stuff

  static Actor getDebugActor(Entity* entity)
  {
    auto box = entity->getFBox();
    auto r = Actor { box.pos, MDL_RECT };
    r.scale = box.size;
    r.zOrder = 10;
    return r;
  }
};

Scene* createPlayingStateAtLevel(View* view, int level)
{
  auto gameState = make_unique<GameState>(view);
  gameState->m_level = level;
  return gameState.release();
}

Scene* createPlayingState(View* view)
{
  return createPlayingStateAtLevel(view, 1);
}

