// Copyright (C) 2018 - Sebastien Alaiwan
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// Main loop timing.
// No game-specific code should be here,
// and no platform-specific code should be here.

#include "app.h"

#include <memory>
#include <string>
#include <vector>

#include "base/geom.h"
#include "base/resource.h"
#include "base/scene.h"
#include "base/view.h"
#include "misc/file.h"
#include "misc/time.h"

#include "audio.h"
#include "audio_backend.h"
#include "display.h"
#include "input.h"
#include "ratecounter.h"
#include "stats.h"

using namespace std;

auto const TIMESTEP = 10;
auto const RESOLUTION = Size2i(512, 512);

Display* createDisplay(Size2i resolution);
MixableAudio* createAudio();
UserInput* createUserInput();

Scene* createGame(View* view, vector<string> argv);

class App : View, public IApp
{
public:
  App(Span<char*> args)
    : m_args({ args.data, args.data + args.len })
  {
    m_display.reset(createDisplay(RESOLUTION));
    m_audio.reset(createAudio());
    m_audioBackend.reset(createAudioBackend(m_audio.get()));
    m_input.reset(createUserInput());

    m_scene.reset(createGame(this, m_args));

    m_lastTime = GetSteadyClockMs();
    m_lastDisplayFrameTime = GetSteadyClockMs();

    registerUserInputActions();
  }

  bool tick() override
  {
    m_input->process();

    auto const now = (int)GetSteadyClockMs();

    if(m_fixedDisplayFramePeriod)
    {
      while(m_lastDisplayFrameTime + m_fixedDisplayFramePeriod < now)
      {
        m_lastDisplayFrameTime += m_fixedDisplayFramePeriod;
        tickOneDisplayFrame(m_lastDisplayFrameTime);
      }
    }
    else
    {
      m_lastDisplayFrameTime = now;
      tickOneDisplayFrame(now);
    }

    return m_running;
  }

private:
  void tickOneDisplayFrame(int now)
  {
    const auto timeStep = m_slowMotion ? TIMESTEP * 10 : TIMESTEP;

    while(m_lastTime + timeStep < now)
    {
      m_lastTime += timeStep;

      if(!m_paused && m_running == 1)
        tickGameplay();
    }

    // draw the frame
    m_actors.clear();
    m_scene->draw();
    draw();

    m_fps.tick(now);
    Stat("FPS", m_fps.slope());

    captureDisplayFrameIfNeeded();
  }

  void captureDisplayFrameIfNeeded()
  {
    if(m_captureFile || m_mustScreenshot)
    {
      vector<uint8_t> pixels(RESOLUTION.width * RESOLUTION.height * 4);
      m_display->readPixels({ pixels.data(), (int)pixels.size() });

      if(m_captureFile)
        fwrite(pixels.data(), 1, pixels.size(), m_captureFile);

      if(m_mustScreenshot)
      {
        File::write("screenshot.rgba", pixels);
        fprintf(stderr, "Saved screenshot to 'screenshot.rgba'\n");

        m_mustScreenshot = false;
      }
    }
  }

  void tickGameplay()
  {
    m_control.debug = m_debugMode;

    auto next = m_scene->tick(m_control);

    if(next != m_scene.get())
    {
      m_scene.release();
      m_scene.reset(next);
      printf("Entering: %s\n", typeid(*next).name());
    }
  }

  void registerUserInputActions()
  {
    // App keys
    m_input->listenToQuit([&] () { m_running = 0; });

    m_input->listenToKey(Key::PrintScreen, [&] (bool isDown) { if(isDown) toggleVideoCapture(); }, true);
    m_input->listenToKey(Key::Return, [&] (bool isDown) { if(isDown) toggleFullScreen(); }, false, true);

    m_input->listenToKey(Key::Y, [&] (bool isDown) { if(isDown && m_running == 2) m_running = 0; });
    m_input->listenToKey(Key::N, [&] (bool isDown) { if(isDown && m_running == 2) m_running = 1; });

    // Player keys
    m_input->listenToKey(Key::Esc, [&] (bool isDown) { if(isDown) onQuit(); });
    m_input->listenToKey(Key::Return, [&] (bool isDown) { m_control.start = isDown; });

    m_input->listenToKey(Key::Left, [&] (bool isDown) { m_control.left = isDown; });
    m_input->listenToKey(Key::Right, [&] (bool isDown) { m_control.right = isDown; });
    m_input->listenToKey(Key::Up, [&] (bool isDown) { m_control.up = isDown; });
    m_input->listenToKey(Key::Down, [&] (bool isDown) { m_control.down = isDown; });

    m_input->listenToKey(Key::Z, [&] (bool isDown) { m_control.fire = isDown; });
    m_input->listenToKey(Key::X, [&] (bool isDown) { m_control.jump = isDown; });
    m_input->listenToKey(Key::C, [&] (bool isDown) { m_control.dash = isDown; });

    m_input->listenToKey(Key::R, [&] (bool isDown) { m_control.restart = isDown; });

    // Debug keys
    m_input->listenToKey(Key::F2, [&] (bool isDown) { if(isDown) m_scene.reset(createGame(this, m_args)); });
    m_input->listenToKey(Key::Tab, [&] (bool isDown) { if(isDown) m_slowMotion = !m_slowMotion; });
    m_input->listenToKey(Key::ScrollLock, [&] (bool isDown) { if(isDown) m_debugMode = !m_debugMode; });
    m_input->listenToKey(Key::Pause, [&] (bool isDown) { if(isDown){ playSound(0); m_paused = !m_paused; } });
  }

  void draw()
  {
    m_display->beginDraw();

    for(auto& actor : m_actors)
    {
      auto where = Rect2f(actor.pos.x, actor.pos.y, actor.scale.width, actor.scale.height);
      m_display->drawActor(where, actor.angle, !actor.screenRefFrame, (int)actor.model, actor.effect == Effect::Blinking, actor.action, actor.ratio, actor.zOrder);
    }

    if(m_running == 2)
      m_display->drawText(Vector2f(0, 0), "QUIT? [Y/N]");
    else if(m_paused)
      m_display->drawText(Vector2f(0, 0), "PAUSE");
    else if(m_slowMotion)
      m_display->drawText(Vector2f(0, 0), "SLOW-MOTION MODE");

    if(m_control.debug)
    {
      for(int i = 0; i < getStatCount(); ++i)
      {
        char txt[256];
        auto stat = getStat(i);
        snprintf(txt, sizeof txt, "%s: %.2f", stat.name, stat.val);
        m_display->drawText(Vector2f(0, 4 - i), txt);
      }
    }

    if(m_textboxDelay > 0)
    {
      auto y = 2.0f;
      auto const DELAY = 90.0f;

      if(m_textboxDelay < DELAY)
        y += 16 * (DELAY - m_textboxDelay) / DELAY;

      m_display->drawText(Vector2f(0, y), m_textbox.c_str());
      m_textboxDelay--;
    }

    m_display->endDraw();
  }

  void onQuit()
  {
    if(m_running == 2)
      m_running = 1;
    else
      m_running = 2;
  }

  void toggleVideoCapture()
  {
    if(!m_captureFile)
    {
      if(m_fullscreen)
      {
        fprintf(stderr, "Can't capture video in fullscreen mode\n");
        return;
      }

      m_captureFile = fopen("capture.rgba", "wb");

      if(!m_captureFile)
      {
        fprintf(stderr, "Can't start video capture!\n");
        return;
      }

      m_fixedDisplayFramePeriod = 40;
      fprintf(stderr, "Capturing video at %d Hz...\n", 1000 / m_fixedDisplayFramePeriod);
    }
    else
    {
      fprintf(stderr, "Stopped video capture\n");
      fclose(m_captureFile);
      m_captureFile = nullptr;
      m_fixedDisplayFramePeriod = 0;
    }
  }

  void toggleFullScreen()
  {
    if(m_captureFile)
    {
      fprintf(stderr, "Can't toggle full-screen during video capture\n");
      return;
    }

    m_fullscreen = !m_fullscreen;
    m_display->setFullscreen(m_fullscreen);
  }

  // View implementation
  void setTitle(char const* gameTitle) override
  {
    m_display->setCaption(gameTitle);
  }

  void preload(Resource res) override
  {
    switch(res.type)
    {
    case ResourceType::Sound:
      m_audio->loadSound(res.id, res.path);
      break;
    case ResourceType::Model:
      m_display->loadModel(res.id, res.path);
      break;
    }
  }

  void textBox(char const* msg) override
  {
    m_textbox = msg;
    m_textboxDelay = 60 * 2;
  }

  void playMusic(MUSIC musicName) override
  {
    if(m_currMusicName == musicName)
      return;

    stopMusic();

    char buffer[256];
    String path;
    path.data = buffer;
    path.len = sprintf(buffer, "res/music/music-%02d.ogg", musicName);
    m_audio->loadSound(1024, path);

    m_musicVoice = m_audio->createVoice();

    m_audio->playVoice(m_musicVoice, 1024, true);
    m_currMusicName = musicName;
  }

  void stopMusic()
  {
    if(m_musicVoice == -1)
      return;

    m_audio->stopVoice(m_musicVoice); // maybe add a fade out here?
    m_audio->releaseVoice(m_musicVoice, true);
    m_musicVoice = -1;
  }

  Audio::VoiceId m_musicVoice = -1;
  int m_currMusicName = -1;

  void playSound(SOUND soundId) override
  {
    auto voiceId = m_audio->createVoice();
    m_audio->playVoice(voiceId, soundId);
    m_audio->releaseVoice(voiceId, true);
  }

  void setCameraPos(Vector2f pos) override
  {
    m_display->setCamera(pos);
  }

  void setAmbientLight(float amount) override
  {
    m_display->setAmbientLight(amount);
  }

  void sendActor(Actor const& actor) override
  {
    m_actors.push_back(actor);
  }

  int m_running = 1;
  int m_fixedDisplayFramePeriod = 0;
  FILE* m_captureFile = nullptr;
  bool m_mustScreenshot = false;

  bool m_debugMode = false;

  int m_lastTime;
  int m_lastDisplayFrameTime;
  RateCounter m_fps;
  Control m_control {};
  vector<string> m_args;
  unique_ptr<Scene> m_scene;
  bool m_slowMotion = false;
  bool m_fullscreen = false;
  bool m_paused = false;
  unique_ptr<MixableAudio> m_audio;
  unique_ptr<IAudioBackend> m_audioBackend;
  unique_ptr<Display> m_display;
  vector<Actor> m_actors;
  unique_ptr<UserInput> m_input;

  string m_textbox;
  int m_textboxDelay = 0;
};

///////////////////////////////////////////////////////////////////////////////

unique_ptr<IApp> createApp(Span<char*> args)
{
  return make_unique<App>(args);
}

