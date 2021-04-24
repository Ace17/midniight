// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

#include "base/scene.h"
#include "base/util.h"

#include "gameplay/collision_groups.h"
#include "gameplay/entity.h"
#include "gameplay/entity_factory.h"
#include "gameplay/models.h"
#include "gameplay/move.h"
#include "gameplay/player.h"
#include "gameplay/sounds.h"
#include "gameplay/toggle.h"

#include "hero.h"

auto const WALK_SPEED = 0.075f;
auto const MAX_HORZ_SPEED = 0.2f;
auto const MAX_FALL_SPEED = 0.2f;
auto const CLIMB_DELAY = 10;
auto const HURT_DELAY = 50;
auto const JUMP_VEL = 0.15;
auto const MAX_LIFE = 31;

enum ORIENTATION
{
  LEFT,
  RIGHT,
};

static auto const NORMAL_SIZE = Size(0.7, 1.9);

struct Rockman : Player, Damageable
{
  Rockman(IEntityConfig*)
  {
    size = NORMAL_SIZE;
    collidesWith |= CG_LADDER;
    Body::onCollision = [this] (Body* other) { onCollide(other); };
  }

  void onCollide(Body* b)
  {
    if(dynamic_cast<Climbable*>(b))
    {
      ladderDelay = 10;
      ladderX = b->pos.x;
    }
  }

  virtual void addActors(vector<Actor>& actors) const override
  {
    auto r = Actor { pos, MDL_ROCKMAN };
    r.scale = Size(3, 3);

    // re-center
    r.pos += Vector(-(r.scale.width - size.width) * 0.5, -0.1);

    if(ball)
    {
      r.action = ACTION_BALL;
      r.ratio = (time % 30) / 30.0f;
    }
    else if(sliding)
    {
      if(shootDelay == 0)
        r.action = ACTION_SLIDE;
      else
        r.action = ACTION_SLIDE_SHOOT;

      r.ratio = (time % 30) / 30.0f;
    }
    else if(hurtDelay || life < 0)
    {
      r.action = ACTION_HURT;
      r.ratio = 1.0f - hurtDelay / float(HURT_DELAY);
    }
    else if(ladder)
    {
      r.action = ACTION_LADDER;
      r.ratio = vel.y == 0 ? 0.3 : (time % 40) / 40.0f;
      r.pos += Vector(0.05, -0.5);
    }
    else if(!ground)
    {
      if(climbDelay)
      {
        r.action = ACTION_CLIMB;
        r.ratio = 1.0f - climbDelay / float(CLIMB_DELAY);
        r.scale.width *= -1;
      }
      else
      {
        r.pos.y -= 0.3;
        r.action = shootDelay ? ACTION_FALL_SHOOT : ACTION_FALL;
        r.ratio = vel.y > 0 ? 0 : 1;
      }
    }
    else
    {
      if(vel.x != 0)
      {
        if(dashDelay)
        {
          r.ratio = min(40 - dashDelay, 40) / 10.0f;
          r.action = ACTION_DASH;
        }
        else
        {
          r.ratio = (time % 50) / 50.0f;

          if(shootDelay == 0)
            r.action = ACTION_WALK;
          else
            r.action = ACTION_WALK_SHOOT;
        }
      }
      else
      {
        if(shootDelay == 0)
        {
          r.ratio = (time % 300) / 300.0f;
          r.action = ACTION_STAND;
        }
        else
        {
          r.ratio = 0;
          r.action = ACTION_STAND_SHOOT;
        }
      }
    }

    if(dir == LEFT)
      r.scale.width *= -1;

    if(blinking)
      r.effect = Effect::Blinking;

    r.zOrder = 1;

    actors.push_back(r);
  }

  void think(Control const& c) override
  {
    control = c;
  }

  float health() override
  {
    return ::clamp(life / float(MAX_LIFE), 0.0f, 1.0f);
  }

  int getArtifactCount() const override { return artifactCount; };
  void addArtifact() override
  {
    ++artifactCount;
    game->getVariable(-2)->set(artifactCount);

    char msg[256];
    sprintf(msg, "artifacts: %d", artifactCount);
    game->textBox(msg);

    blinking = 50;
  }

  virtual void addUpgrade(int upgrade) override
  {
    upgrades |= upgrade;
    blinking = 200;
    life = MAX_LIFE;
    game->getVariable(-1)->set(upgrades);
  }

  void computeVelocity(Control c)
  {
    airMove(c);

    if(ground)
      doubleJumped = false;

    if(vel.x > 0)
      dir = RIGHT;

    if(vel.x < 0)
      dir = LEFT;

    // gravity
    if(life > 0 && !ladder)
      vel.y -= 0.005;

    sliding = false;

    if(upgrades & UPGRADE_SLIDE)
    {
      if(!ball && !ground)
      {
        if(vel.y < 0 && facingWall() && (c.left || c.right))
        {
          // don't allow double-jumping from sliding state,
          // unless we have the climb upgrade
          doubleJumped = !(upgrades & UPGRADE_CLIMB);

          for(int i = 0; i < 8; ++i)
            vel.y *= 0.97;

          sliding = true;
          dashDelay = 0;
        }
      }
    }

    if(jumpbutton.toggle(c.jump))
    {
      if(ground)
      {
        game->playSound(SND_JUMP);
        vel.y = JUMP_VEL;
        doubleJumped = false;
      }
      else if(facingWall() && (upgrades & UPGRADE_CLIMB))
      {
        game->playSound(SND_JUMP);
        // wall climbing
        vel.x = dir == RIGHT ? -0.04 : 0.04;

        if(c.dash)
          dashDelay = 40;
        else
          dashDelay = 0;

        vel.y = JUMP_VEL;
        climbDelay = CLIMB_DELAY;
        doubleJumped = false;
      }
      else if((upgrades & UPGRADE_DJUMP) && !doubleJumped)
      {
        game->playSound(SND_JUMP);
        vel.y = JUMP_VEL;
        doubleJumped = true;
      }
    }

    if(!ladder)
    {
      // stop jump if the player release the button early
      if(vel.y > 0 && !c.jump)
        vel.y = 0;
    }

    vel.x = ::clamp(vel.x, -MAX_HORZ_SPEED, MAX_HORZ_SPEED);
    vel.y = max(vel.y, -MAX_FALL_SPEED);
  }

  void translate(Vector2f tx) override
  {
    pos += tx;
    ladderX += tx.x;
  }

  void airMove(Control c)
  {
    float wantedSpeed = 0;

    if(ladderDelay && (c.up || c.down))
      ladder = true;

    if(ladder)
    {
      pos.x = ladderX + 0.1;

      if(c.jump || c.left || c.right)
      {
        ladder = false;
      }
      else
      {
        if(c.up)
          vel.y = +WALK_SPEED * 0.75;
        else if(c.down)
          vel.y = -WALK_SPEED * 0.75;
        else
          vel.y = 0;
      }
    }

    if(!climbDelay && !ladder)
    {
      if(c.left)
        wantedSpeed -= WALK_SPEED;

      if(c.right)
        wantedSpeed += WALK_SPEED;
    }

    if(upgrades & UPGRADE_DASH)
    {
      if(dashbutton.toggle(c.dash) && ground && dashDelay == 0)
      {
        game->playSound(SND_JUMP);
        dashDelay = 40;
      }
    }

    if(dashDelay > 0)
    {
      wantedSpeed *= 4;
      vel.x = wantedSpeed;
    }

    for(int i = 0; i < 10; ++i)
      vel.x = (vel.x * 0.95 + wantedSpeed * 0.05);

    if(abs(vel.x) < 0.001)
      vel.x = 0;
  }

  virtual void tick() override
  {
    decrement(blinking);
    decrement(hurtDelay);

    if(ground)
      decrement(dashDelay);

    if(hurtDelay || life <= 0)
      control = Control {};

    if(restartbutton.toggle(control.restart))
      life = 0;

    // 'dying' animation
    if(life <= 0)
    {
      decrement(dieDelay);

      if(dieDelay < 100)
        game->setAmbientLight((dieDelay - 100) / 100.0);

      if(dieDelay == 0)
        respawn();
    }

    time++;
    computeVelocity(control);

    auto trace = slideMove(this, vel);

    if(!trace.vert)
      vel.y = 0;

    auto const wasOnGround = ground;

    // probe for solid ground
    {
      Box box;
      box.pos.x = pos.x;
      box.pos.y = pos.y - 0.1;
      box.size.width = size.width;
      box.size.height = 0.1;
      ground = physics->isSolid(this, roundBox(box));
    }

    if(ground && !wasOnGround)
    {
      if(vel.y < 0)
      {
        if(tryActivate(debounceLanding, 15))
          game->playSound(SND_LAND);

        dashDelay = 0;
      }
    }

    decrement(footstepDelay);

    if(ground && (control.left || control.right))
    {
      if(footstepDelay == 0)
      {
        footstepDelay = 30;
        game->playSound(SND_FOOTSTEP_1);
      }
    }

    decrement(debounceFire);
    decrement(debounceLanding);
    decrement(climbDelay);
    decrement(shootDelay);
    decrement(ladderDelay);

    handleBall();
    collisionGroup = CG_PLAYER;

    if(!blinking)
      collisionGroup |= CG_SOLIDPLAYER;
  }

  virtual void onDamage(int amount) override
  {
    if(life <= 0)
      return;

    if(!blinking)
    {
      life -= amount;

      if(life < 0)
      {
        die();
        return;
      }

      hurtDelay = HURT_DELAY;
      blinking = 200;
      game->playSound(SND_HURT);
    }
  }

  bool facingWall() const
  {
    auto const front = dir == RIGHT ? 0.7 : -0.7;

    Box box;
    box.pos.x = pos.x + size.width / 2 + front;
    box.pos.y = pos.y + 0.3;
    box.size.width = 0.01;
    box.size.height = 0.9;

    if(physics->isSolid(this, roundBox(box)))
      return true;

    return false;
  }

  void enter() override
  {
    game->setAmbientLight(0);
    upgrades = game->getVariable(-1)->get();
    artifactCount = game->getVariable(-2)->get();
  }

  void die()
  {
    game->playSound(SND_DIE);
    ball = false;
    size = NORMAL_SIZE;
    dieDelay = 150;
  }

  void respawn()
  {
    game->respawn();
    blinking = 20;
    vel = NullVector;
    life = MAX_LIFE;
  }

  void handleBall()
  {
    if(!ladder && control.down && !ball && (upgrades & UPGRADE_BALL))
    {
      ball = true;
      size = Size(NORMAL_SIZE.width, 0.9);
    }

    if(control.up && ball)
    {
      Box box;
      box.size = NORMAL_SIZE;
      box.pos = pos;

      if(!physics->isSolid(this, roundBox(box)))
      {
        ball = false;
        size = NORMAL_SIZE;
      }
    }
  }

  int debounceFire = 0;
  int debounceLanding = 0;
  ORIENTATION dir = RIGHT;
  bool ground = false;
  Toggle jumpbutton, firebutton, dashbutton, restartbutton;
  int time = 0;
  int climbDelay = 0;
  int hurtDelay = 0;
  int dashDelay = 0;
  int dieDelay = 0;
  int shootDelay = 0;
  int ladderDelay = 0;
  int footstepDelay = 0;
  float ladderX;
  int life = MAX_LIFE;
  bool doubleJumped = false;
  bool ball = false;
  bool sliding = false;
  bool ladder = false;
  Control control {};
  Vector vel;

  int upgrades = 0;
  int artifactCount = 0;
};

std::unique_ptr<Player> makeRockman()
{
  return make_unique<Rockman>(nullptr);
}

DECLARE_ENTITY("Hero", Rockman);

