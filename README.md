# Barnes-Hut-Simulator

The Barnes-Hut Algorithm describes an effective method for solving n-body problems. It was originally published in 1986 by Josh Barnes and Piet Hut. Instead of directly summing up all forces, it is using a tree based approximation scheme which reduces the computational complexity of the problem from O(N2) to O(N log N). This repository belongs to an article on [beltoforion.de](https://beltoforion.de/en) explaining the Barnes-Hut algorithm. 

English Article:
* [Barnes-Hut galaxy simulator](https://beltoforion.de/en/barnes-hut-galaxy-simulator/)

German Article:
* [Barnes-Hut Galaxiensimulation](https://beltoforion.de/de/barnes-hut-galaxiensimulation/)

![](https://beltoforion.de/en/barnes-hut-galaxy-simulator/images/anim.gif)

## Kotlin/LWJGL GPU simulation

This repository now also contains a Kotlin implementation that offloads the
gravity calculation to the GPU via OpenGL 4.6 compute shaders. The project is
configured with Gradle:

```bash
gradle run  # use ./gradlew run if you generate a Gradle wrapper
```

The new application lives under `src/main/kotlin` and uses LWJGL to drive both
the compute shader and the point rendering pipeline.
