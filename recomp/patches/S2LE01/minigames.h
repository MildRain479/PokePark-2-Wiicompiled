/* PokePark 2's four attractions, as AttractionMenuRes.dac's rules say. */
static const struct {
    int at, hold, use;
} kMinigames[] = {
    {1, HOLD_SIDEWAYS, USE_POINTER},          /* Cake Contraption: point, A shoots */
    {2, HOLD_UPRIGHT, USE_SWING},             /* Dance Inferno: held upright, dancing with the music */
    {3, HOLD_SIDEWAYS, USE_SHAKE | USE_ANY_WAY}, /* Power Bash: swing, A drops a bomb through the floor */
    {4, HOLD_SIDEWAYS, USE_STEER | USE_PITCH}, /* Flight of Fancy: tilted, 2 flies faster */
};
