"""Integrate the shader's Charlie sheen response against a white hemisphere.

This checks the conservative analytic energy estimate used for base-layer
attenuation. It is a numeric contract for the chosen approximation, not a GPU
image-quality test.
"""

from math import cos, exp, pi, sin, sqrt


def lambda_fit(cosine: float, alpha: float) -> float:
    one_minus_alpha_sq = (1.0 - alpha) ** 2
    a = 21.5473 + (25.3245 - 21.5473) * one_minus_alpha_sq
    b = 3.82987 + (3.32435 - 3.82987) * one_minus_alpha_sq
    c = 0.19823 + (0.16801 - 0.19823) * one_minus_alpha_sq
    d = -1.97760 + (-1.27393 + 1.97760) * one_minus_alpha_sq
    e = -4.32054 + (-4.85967 + 4.32054) * one_minus_alpha_sq
    return a / (1.0 + b * cosine**c) + d * cosine + e


def charlie_lambda(cosine: float, alpha: float) -> float:
    if cosine < 0.5:
        return exp(lambda_fit(cosine, alpha))
    return exp(2.0 * lambda_fit(0.5, alpha) - lambda_fit(1.0 - cosine, alpha))


def normalize_sheen(view_cosine: float, roughness: float) -> float:
    return 1.0 + 5.0 * (1.0 - roughness) ** 4 * exp(-view_cosine / 0.02)


def energy_estimate(view_cosine: float, roughness: float) -> float:
    return min(1.0, (1.0 - view_cosine) ** 2 + 0.25 * roughness + 0.1)


def directional_albedo(view_cosine: float, roughness: float, samples: int = 128) -> float:
    alpha = max(roughness * roughness, 0.002)
    view_sine = sqrt(1.0 - view_cosine * view_cosine)
    view_lambda = charlie_lambda(view_cosine, alpha)
    integral = 0.0
    for row in range(samples):
        light_cosine = (row + 0.5) / samples
        light_sine = sqrt(1.0 - light_cosine * light_cosine)
        light_lambda = charlie_lambda(light_cosine, alpha)
        visibility = 1.0 / (
            (1.0 + view_lambda + light_lambda)
            * 4.0
            * view_cosine
            * light_cosine
        )
        for column in range(samples):
            phase = (column + 0.5) * 2.0 * pi / samples
            hx = view_sine + light_sine * cos(phase)
            hy = light_sine * sin(phase)
            hz = view_cosine + light_cosine
            normal_half_cosine = hz / sqrt(hx * hx + hy * hy + hz * hz)
            sine_sq = max(0.0, 1.0 - normal_half_cosine**2)
            distribution = (2.0 + 1.0 / alpha) * sine_sq ** (0.5 / alpha) / (2.0 * pi)
            integral += distribution * visibility * light_cosine
    return (
        integral
        * (2.0 * pi / (samples * samples))
        / normalize_sheen(view_cosine, roughness)
    )


def main() -> None:
    roughness_values = (0.045, 0.06, 0.08, 0.1, 0.15, 0.2, 0.3,
                        0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0)
    view_cosines = (0.001, 0.002, 0.005, 0.01, 0.02, 0.03, 0.05,
                    0.07, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7,
                    0.8, 0.9, 1.0)
    maximum_albedo = 0.0
    maximum_bound_excess = 0.0
    for roughness in roughness_values:
        for view_cosine in view_cosines:
            albedo = directional_albedo(view_cosine, roughness)
            maximum_albedo = max(maximum_albedo, albedo)
            maximum_bound_excess = max(
                maximum_bound_excess,
                albedo - energy_estimate(view_cosine, roughness),
            )
    print(f"Charlie sheen: {len(roughness_values) * len(view_cosines)} cases; "
          f"max directional albedo={maximum_albedo:.6f}; "
          f"max estimate excess={maximum_bound_excess:.6f}")
    if maximum_albedo > 1.01 or maximum_bound_excess > 0.01:
        raise SystemExit("Sheen exceeds the white-environment energy bound")


if __name__ == "__main__":
    main()
