# Network Tank Arena: Client-Side Prediction and Server Reconciliation

This is my version of the Network Arena starter (Unreal track). My focus was movement under latency. The starter moves every tank on the server and the clients just wait for the replicated position, which makes driving your own tank feel choppy and laggy. I replaced that with numbered movement commands, client-side prediction, server reconciliation and entity interpolation for the other tanks, while keeping the server fully authoritative over movement.

Built on **Unreal Engine 5.8**. The startup level is `TestMap`.
Older Unreal versions may or may not work, only tested on 5.8.3

## The problem with the starter

In the starter, the client builds an intent (throttle, turn, aim) every frame but only sends it to the server every 50ms through an unreliable RPC. The server stores the latest intent and simulates the tank with it every server tick, using the server's own DeltaTime. The client then has to wait for the replicated position (20hz) to see its own tank move.

So the client's own movement is delayed by a full round trip plus up to 50ms of send timer plus up to 50ms of replication, and it moves in 20hz steps. On top of that the server never knows how long an input was actually held. It just keeps running the latest intent until a new one arrives, so it can keep driving after the player let go, or stop too early. This leads to increasing amounts of error between what the client meant to do and what the server thinks is the truth.

The bigger issue is that this model can't be fixed by just adding prediction. There is only ever "the current intent", so there is nothing to replay and nothing to compare against. To predict and reconcile, the server needs to know exactly which inputs it has processed, and the client needs to be able to re-run the ones it hasn't. That needs numbered commands.

## What I implemented

I did this in small steps and measured after each one. Roughly in order:

1. **Numbered movement commands.** `FTankMovementCommand` wraps the existing intent with a `DeltaTime` and a `uint32` sequence number. The client records one command per frame and the server simulates each one with the DeltaTime the client recorded, instead of guessing from when packets arrive. I changed the intent to send a computed aim yaw instead of a world aim point, to save two floats per command.
2. **`UTankCommandSubsystem`.** A world subsystem that owns the whole command flow for every player: recording, sending, receiving, acknowledging, prediction and reconciliation. This keeps the logic and the `HasAuthority` checks out of the PlayerController and Pawn. Commands are stored in `FTankCommandRingBuffer`, a fixed size buffer of 256 commands.
3. **Acknowledgement and resending.** After each batch the server sends back the last processed sequence number. The client drops everything up to that number and resends all unacknowledged commands in every batch (oldest first, max 64), so a lost packet just means the commands arrive 50ms later inside the next batch.
4. **Server state in the acknowledgement.** The acknowledgement also carries the tank's location and chassis yaw after the last processed command. The owning client's tank is now an autonomous proxy (the starter forced it to be a simulated proxy) and is positioned from these acknowledgements instead of normal replicated movement.
5. **Client-side prediction and server reconciliation.** The owning client runs every command on its own tank as soon as it's recorded, so the tank responds the same frame. When an acknowledgement arrives the client snaps to the server's state and replays every command the server hasn't processed yet. If they agree nothing visible happens, if they don't the tank ends up where the server says plus the input still in flight.
6. **Server time budget.** The server limits how much command time each remote player can spend to the time that has actually passed (with a 0.25s cap). This stops a speed hack where the client claims bigger DeltaTimes than it really had.
7. **Entity interpolation for the other tanks.** Other players' tanks are drawn 100ms in the past, interpolated between the updates around that time, instead of jumping to each update as it arrives. This runs on both the clients and the host.

I also added some tools to test all of this:

- `MeasureDrive <Seconds> <Runs>` drives the local tank by script and compares how far the server actually moved it with how far it should have moved.
- Mock cheat commands for clients (`CheatSpeed`, `CheatTeleport`, `CheatDeltaTime`, `CheatReset`) to force the client and server to disagree, so corrections can be tested.
- A `tank.TimeBudget` console variable to switch the time budget off and on, so its effect can be compared in one session.
- Every correction above 0.1cm or 0.1 degrees is logged to `LogTankPrediction`, and every time budget clamp to `LogTankTimeBudget`.
- Automation tests for all of the above (`NetworkTankArena.*`).

## Authority and replication

- **The server owns all movement.** Clients only send input. The client predicts its own tank, but the server's state always wins on reconciliation.
- **Commands are sent as an unreliable server RPC**, batched 20 times per second. I chose unreliable over reliable since a reliable RPC would stall everything behind a lost packet. Lost commands are covered by resending instead.
- **Acknowledgements are an unreliable client RPC.** They are cumulative, so a lost one is fixed by the next one, and a late or reordered one is ignored instead of snapping the tank backwards. The tank is sent along so a late acknowledgement for a previous pawn can't move a new one.
- **Every command is sanitized on both sides.** Axes are clamped, non-finite values are rejected, and DeltaTime is capped at 0.1s per command. The server also checks that the controller actually owns the tank it sends commands for, and rejects batches over 64 commands.
- **Only the server and the owning client simulate movement.** Other players' tanks on a client are simulated proxies and only follow replicated movement.
- **Turret yaw** is replicated state with `COND_SkipOwner`, since the owning client predicts its own aim and the server's value would only overwrite it with an older one.
- **Interpolation is presentation only.** It moves the tank's visuals, the actor and its collision stay at the real gameplay state.

## Testing

I tested in PIE with 2 players (Play As Listen Server) under three conditions, using Unreal's built-in network emulation profiles:

| Profile      | Incoming latency | Outgoing latency | Incoming / outgoing packet loss |
| ------------ | ---------------- | ---------------- | ------------------------------- |
| No emulation | none             | none             | none                            |
| Average      | 30 to 60ms       | 30 to 60ms       | 1% / 1%                         |
| Bad          | 100 to 200ms     | 100 to 200ms     | 5% / 5%                         |

These are set under **Network Emulation** in the PIE play settings, using the built-in Average and Bad profiles, with **Emulation Target = Clients Only**. Only the client's connection is impaired, so under Bad the round trip is about 200 to 400ms (I measured 323 to 363ms).

All drive measurements are `MeasureDrive 2 30`, run from the client window: 30 runs of driving for 2 seconds, alternating forward and reverse. The error is the difference between how far the server moved the tank and how far it should have moved (speed × time the throttle was held). Average absolute error after each step:

| Step                          | No emulation | Average | Bad   |
| ----------------------------- | ------------ | ------- | ----- |
| Starter (intent model)        | 0.99%        | 1.10%   | 1.66% |
| Commands, no resending        | 0.00%        | 1.00%   | 5.03% |
| With acknowledgement + resend | 0.00%        | 0.04%   | 0.00% |
| With prediction + reconciliation | 0.00%     | 0.00%   | 0.00% |

The drive test only measures the server, so it can't show how good the prediction is. Every run ends after everything is acknowledged, and by then the client has been reconciled to the server. For the prediction itself I counted corrections in `LogTankPrediction`:

| Scenario (`MeasureDrive 2 30`, Bad) | Corrections | Time budget clamps | Drive error |
| ----------------------------------- | ----------- | ------------------ | ----------- |
| Honest                              | 0           | 0                  | 0.00%       |
| `CheatSpeed 2`                      | 1091        | 1 (16.7ms)         | 0.03%       |

Over 30 runs under Bad (measured ping 323 to 363ms, 0.9% to 8.5% loss per run) the honest client was never corrected once. Client and server run the same commands with the same DeltaTime, so they end up in exactly the same place. The time budget didn't clamp during that run either.

With `CheatSpeed 2` the client predicts at double speed but sends honest commands, so the server disagrees all the time. It was corrected 1091 times over 30 runs, about once per acknowledgement while driving. The median correction was 25cm, which is exactly the extra distance double speed predicts during one 50ms batch, and the largest was 100cm when a few acknowledgements in a row were lost. The drive error stayed at almost 0, so the cheat only changed what the cheater saw.

The one error in that run was a -0.83% undershoot, and during the same run the server logged a 16.7ms time budget clamp. 16.7ms at 500cm/s is 8.3cm, which is exactly the error. `CheatSpeed` doesn't change command time, so this was an honest frame getting clamped. While lag holds commands back the budget sits at its cap and the extra server time is thrown away, so when the delayed commands arrive together the burst can be slightly bigger than the budget. It happened once in 60 runs under Bad.

For the time budget:

| `MeasureDrive 2 30`, Bad, `CheatDeltaTime 2` | Runs | Avg error | Avg abs | Min | Max |
| -------------------------------------------- | ---- | --------- | ------- | --- | --- |
| Time budget off (`tank.TimeBudget 0`)        | 30   | +100.00%  | 100.00% | +100.00% | +100.00% |
| Time budget on                               | 28   | -0.13%    | 0.94%   | -3.33%   | +2.08%   |

Without the budget the speed hack works perfectly, every run is exactly +100%. With it the hack gains nothing: the server clamped about 3.7s of command time per run, which is the length of each run. I expected up to +12.5% since the budget can hold 0.25s, but `CheatDeltaTime` also inflates time while the tank stands still between runs, so the budget is always empty when a drive starts. A cheater that only inflates time while driving could still gain up to the cap. The small spread left (about one 50ms batch) is from batch boundaries.

I left out the first two runs of the "on" measurement. The client window wasn't focused, so the editor throttled it to about 3fps, and every frame got capped at 0.1s when turned into a command. Both runs came out at exactly -70% (0.6s of 2s sent), which is the slow frame effect again and not the budget.

## A problem I diagnosed: lost batches

When I first switched to commands (still server-side movement, no resending), the error without emulation dropped to exactly 0%, but under Average and Bad it got *worse* than the starter. That looked odd at first, since the new model should be more accurate.

Looking at the numbers, every error was an undershoot, never an overshoot, and the average error matched the emulated packet loss almost exactly: 1% loss gave -1.00%, and 5% loss gave -5.03%. Each batch holds 50ms of commands, which is 2.5% of a 2 second drive, so the worst runs lined up with whole batches too (-7.50% is exactly three lost batches, -12.08% about five). So the cause was lost packets. Every lost batch was movement the server simply never ran.

The starter hid this, since a lost intent was just replaced by the next one and the server kept running the old intent in the meantime. That filled the gap, but it's also why the starter had timing errors in both directions.

The fix was acknowledgement and resending (step 3). After that, Bad went from -5.03% to 0.00%.

There was one -1.05% run left under Average. 1.05% of 2 seconds is about 21ms, which is less than one batch, so it couldn't be a lost batch. The most likely cause is a single slow frame of around 121ms: frames longer than 0.1s are clamped to 0.1s when turned into a command, while `MeasureDrive` still counts the whole frame as held throttle. Client and server both clamp the same way, so they still agree.

## Why prediction, and the other improvements

Prediction and reconciliation was the main improvement, since it's what actually fixes the feel. Your own tank now responds the same frame no matter the latency, while the server stays fully authoritative. The other steps were needed to make it correct:

- Numbered commands with their own DeltaTime made the server's movement exact (0% error without emulation) and made replay possible at all.
- Acknowledgement and resending made it correct under packet loss.
- The time budget closes a speed hack that the command model opened up. Before it, a client could claim up to six times its real frame time at 60fps and the server would accept it, since each command was still under 0.1s.
- Interpolation fixes the other tanks, which can't be predicted.

## Trade-offs and limitations

- **Bandwidth.** Resending costs bandwidth, since every batch now carries roughly a round trip's worth of commands instead of the last 50ms. Under Bad at 60fps that's around 20 to 30 commands per batch instead of 3.
- **Collisions with other tanks** are the main source of corrections. The client sweeps against where it currently sees the other tanks, which is behind the server.
- **Corrections snap** instead of being smoothed. Simpler, and errors are easy to see and measure, but a correction is a visible pop.
- **Interpolated tanks are drawn 100ms behind** their collision, so you can hit a moving tank slightly before it looks like you touch it.
- **Interpolation uses receive time**, not server timestamps. Under Bad the jitter is about as large as the 100ms delay, so a late update can make another tank briefly pause or move unevenly. A larger delay or server timestamps would fix that.
- **The time budget cap is a trade-off.** A small cap can clamp honest players when lag makes commands arrive in bursts, while a larger cap lets a cheater save up more time. With the 0.25s cap, one honest frame (16.7ms) was clamped in 60 runs under Bad. Raising it to 0.5s should remove that, but doubles what a cheater can save up. Going slower than real time is allowed on purpose, since it only slows the player down, and honest clients already send less time when a slow frame is clamped.
- **DeltaTime is variable**, not a fixed timestep. Simpler to implement, but the client is still trusted with its frame times.
- **Aim is trusted.** The client sends its computed aim yaw. Aim hacks can't really be stopped by the server since they just fake input. The same goes for bots, wallhacks etc. If a hack only relies on emulating normal player input or recieving positions of other replicated players, it cannot easily be stopped by the server without dedicated anti-cheat.
- **Teleports and respawns** would slide across the map on other clients, since nothing clears the interpolation buffer. The game doesn't have any functionality like that (atleast in my version), so I did not spend any time on handling that.
- **Host's remote tanks** step at the 20hz command rate in gameplay terms. Interpolation smooths how they look, but the server state itself only moves when a batch arrives. Realistically for a game like this with few players and very little potential other replicated objects, a higher frequency would be ok. But I wanted to stick to 20hz since it would show just how smooth I can make it feel and look without just upping the frequency of replication and sending.

## How to run and test

1. Open `NetworkTankArena.uproject` with UE 5.8 (or older, at your own risk) and build if prompted.
2. Open `TestMap`, go to **Play → Advanced Settings**, set **Number of Players = 2**, **Net Mode = Play As Listen Server**.
3. For impaired network tests, enable **Network Emulation** in editor preferences and pick the **Average** or **Bad** profile.
4. Press Play. W/S drives, A/D turns, the mouse aims the turret. Focus each window to control that player's tank.

The console has these commands. Run them in the **client** window:

| Command                        | What it does                                                                                |
| ------------------------------ | ------------------------------------------------------------------------------------------- |
| `MeasureDrive <Seconds> <Runs>` | Scripted drive test, prints the error per run and a summary                                |
| `CheatSpeed <Multiplier>`      | Client predicts at a scaled speed but sends honest commands. Gets corrected (rubber banding) |
| `CheatTeleport <Distance>`     | Moves the tank forward on the client only. Pulled back on the next acknowledgement          |
| `CheatDeltaTime <Multiplier>`  | Sends inflated command times. Clamped by the server's time budget                           |
| `CheatReset`                   | Turns all cheats off                                                                        |
| `tank.TimeBudget 0` / `1`      | Turns the server's time budget off or on, to compare `CheatDeltaTime` with and without it  |

Filter the Output Log on `LogTankPrediction` to see corrections and `LogTankTimeBudget` to see clamps.

The automation tests are in **Tools → Session Frontend → Automation**, filter on `NetworkTankArena`. You can also run tests in your IDE of choice (Visual Studio, Rider, etc.), I personally ran all tests in Rider during the development.

For a packaged Development build, start two copies, open the console and enter `HostLocalhost` in the first and `JoinLocalhost` in the second (127.0.0.1:7777).

## Where things are

| File                                  | What it holds                                                          |
| ------------------------------------- | ---------------------------------------------------------------------- |
| `TankCommandSubsystem`                | The command flow: recording, batching, acknowledgement, prediction, reconciliation, time budget |
| `TankMovementCommand.h`               | `FTankMovementCommand`                                                 |
| `TankMovementState.h`                 | `FTankMovementState`, the server state sent with acknowledgements      |
| `TankCommandRingBuffer`               | Fixed size command buffer used by both client and server               |
| `TankInterpolationBuffer`             | Timed snapshots for drawing other tanks in the past                    |
| `TankPawn`                            | Applies commands and server state, presentation and interpolation      |
| `TankPlayerController`                | Input, the RPCs, `MeasureDrive` and the cheat commands                 |
| `TankGameplayMath`                    | Sanitization and the movement math shared by client and server         |
| `Tests/`                              | Automation tests                                                       |
