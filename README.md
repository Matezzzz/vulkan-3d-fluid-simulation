# vulkan-3d-fluid-simulation

A 3D fluid simulation on the GPU based on the article 'Fluid flow for the rest of us', available [here](https://cg.informatik.uni-freiburg.de/intern/seminar/gridFluids_fluid_flow_for_the_rest_of_us.pdf).



Position of fluid is tracked using particles. These move according to fluid velocity, all fields in which particles are present are deemed to be filled with fluid.
Solid blocks are present on all domain edges.
Requires vulkan, GLM, ...