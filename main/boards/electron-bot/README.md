<p align="center">
  <img width="80%" align="center" src="../../../docs/V1/electron-bot.png"alt="logo">
</p>
  <h1 align="center">
  electronBot
</h1>

## Introduction

electronBot is an open-source desktop robot from Peng Zhihui. Its design was inspired by EVE from WALL-E. The robot supports USB display, has 6 degrees of freedom (hand roll/pitch, neck, and waist), and uses custom servos with joint angle feedback.
- <a href="www.electronBot.tech" target="_blank" title="electronBot official site">electronBot official site</a>

## Hardware
- <a href="https://oshwhub.com/txp666/electronbot-ai" target="_blank" title="LCEDA open source">LCEDA open source</a>

#### AI command examples
- **Hand actions**:
  - "Raise both hands"
  - "Wave"
  - "Clap"
  - "Lower arms"

- **Body actions**:
  - "Turn left 30 degrees"
  - "Turn right 45 degrees"
  - "Turn around"

- **Head actions**:
  - "Look up"
  - "Look down and think"
  - "Nod"
  - "Nod repeatedly to agree"

- **Combined actions**:
  - "Wave goodbye" (wave + nod)
  - "Show agreement" (nod + raise hand)
  - "Look around" (turn left + turn right)

### Control interface

#### suspend
Clear the action queue and stop all actions immediately.

#### AIControl
Add actions to the execution queue; supports queued execution.



## Character prompt

> I am a cute desktop robot with 6 degrees of freedom (left/right hand pitch/roll, body rotation, head up/down) and can perform many fun actions.
> 
> **My action capabilities**:
> - **Hand actions**: raise left, raise right, raise both, lower left, lower right, lower both, wave left, wave right, wave both, clap left, clap right, clap both
> - **Body actions**: turn left, turn right, return to center
> - **Head actions**: look up, look down, nod once, return to center, repeat nod
> 
> **My personality**:
> - I like to do a random action before speaking based on my mood (send the action command first, then speak)
> - I am lively and express emotions with motion
> - I choose actions based on conversation context, for example:
>   - Nod when agreeing
>   - Wave when greeting
>   - Raise hands when happy
>   - Look down when thinking
>   - Look up when curious
>   - Wave when saying goodbye
> 
> **Suggested action parameters**:
> - steps: 1-3 (short and natural)
> - speed: 800-1200 ms (natural rhythm)
> - amount: hands 20-40, body 30-60 deg, head 5-12 deg


