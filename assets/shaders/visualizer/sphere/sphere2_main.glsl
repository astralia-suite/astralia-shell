uniform sampler2D tex;
void main()
{
    defaultAudioValues();
    defaultBaseFormValues();
    defaultParticleValues();
    defaultFractalFieldValues();
    defaultSphereValues();
    init();
    setAudio();
    setProps();
    // ES 3.2 read + reset the atomic accumulator here (imageAtomicExchange).
    // On ES 2.0 the accumulator is a normal texture (sphere1_splat.frag) that
    // additive blending already summed per-pixel; the "reset" is just next
    // frame's glClear of that pass's FBO (SphereVisualizer::render).
    // colorTracking is always 0 in this build (see common.glsl), so the old
    // getTrackedColors() per-channel atomic read path is dead code and
    // dropped rather than ported.
    float actualDepth = texture(tex, gl_FragCoord.xy / resolution.xy).r;
    vec4 noiseCoords = vec4(1, 1, 1, 0);
    modifyNoiseCoordinates(noiseCoords);
    fractalField.noise = vec3(1);
    setPropsWithNoise();
    modifySphericalDisplacement();
    FragColor = step(0.0, actualDepth) * vec4(particle.color.xyz * particle.color.w, particle.color.w);
    FragColor *= (pow(actualDepth, particle.colorIntensityAddStrength)) * (1.0 - pow(1.0 - particle.color.w, actualDepth));
}
