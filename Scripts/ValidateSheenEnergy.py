"""Check reciprocal Charlie sheen, its directional-albedo fit, and energy."""

from math import cos, exp, log, pi, sin, sqrt
from pathlib import Path
import re


# Chebyshev coefficients for the reciprocal BRDF's white-hemisphere integral.
# Rows are the view-axis order; columns are perceptual-roughness order.
FIT_COEFFICIENTS = (
    (0.4662008734, 0.1421159035, 0.05200258402, -0.05125628079, 0.006825325422, -0.004605599776, 0.002613252967),
    (-0.4067666558, 0.06915140574, -0.1195616388, 0.1024846881, -0.04368122318, 0.02249280341, -0.01160481628),
    (-0.04041981132, -0.1105422938, 0.1245469551, -0.1063839573, 0.06729718862, -0.03703415321, 0.02134558657),
    (0.06907690518, -0.04916388234, 0.003350617615, 0.02625226625, -0.02702190985, 0.02009724266, -0.01436163036),
    (-0.03666481275, 0.04569041403, -0.05400284215, 0.04947314302, -0.03266691141, 0.01681106069, -0.006804344716),
    (0.002019475767, 0.008140849677, 0.01012986063, -0.02356920128, 0.02254739397, -0.0177240175, 0.01245039065),
    (0.009723827867, -0.02778271004, 0.01832648501, -0.01192787424, 0.008339719302, -0.004460323995, 0.001011952775),
)


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


def normalize_sheen(view_cosine: float, light_cosine: float, roughness: float) -> float:
    def grazing_normalization(cosine: float) -> float:
        return 1.0 + 5.0 * (1.0 - roughness) ** 4 * exp(-cosine / 0.02)

    return max(grazing_normalization(view_cosine),
               grazing_normalization(light_cosine))


def energy_estimate(view_cosine: float, roughness: float) -> float:
    return min(1.0, (1.0 - view_cosine) ** 2 + 0.25 * roughness + 0.1)


def fitted_directional_albedo(view_cosine: float, roughness: float) -> float:
    x = 2.0 * log(1.0 + max(view_cosine, 0.001) * 50.0) / log(51.0) - 1.0
    y = 2.0 * max(0.045, min(roughness, 1.0)) - 1.0
    ty = [1.0, y]
    for index in range(2, 7):
        ty.append(2.0 * y * ty[index - 1] - ty[index - 2])
    rows = [sum(FIT_COEFFICIENTS[row][column] * ty[column]
                for column in range(7)) for row in range(7)]
    next_value = 0.0
    next_next_value = 0.0
    for row in range(6, 0, -1):
        current = rows[row] + 2.0 * x * next_value - next_next_value
        next_next_value = next_value
        next_value = current
    return max(0.0, min(1.0, rows[0] + x * next_value - next_next_value + 0.026))


def sheen_brdf(view_cosine: float, light_cosine: float,
               phase: float, roughness: float) -> float:
    alpha = max(roughness * roughness, 0.002)
    view_sine = sqrt(1.0 - view_cosine * view_cosine)
    light_sine = sqrt(1.0 - light_cosine * light_cosine)
    hx = view_sine + light_sine * cos(phase)
    hy = light_sine * sin(phase)
    hz = view_cosine + light_cosine
    normal_half_cosine = hz / sqrt(hx * hx + hy * hy + hz * hz)
    sine_sq = max(0.0, 1.0 - normal_half_cosine**2)
    distribution = (2.0 + 1.0 / alpha) * sine_sq ** (0.5 / alpha) / (2.0 * pi)
    visibility = 1.0 / ((1.0 + charlie_lambda(view_cosine, alpha) +
                         charlie_lambda(light_cosine, alpha)) *
                        4.0 * view_cosine * light_cosine)
    return distribution * visibility / normalize_sheen(
        view_cosine, light_cosine, roughness)


def directional_albedo(view_cosine: float, roughness: float, samples: int = 128) -> float:
    alpha = max(roughness * roughness, 0.002)
    view_sine = sqrt(1.0 - view_cosine * view_cosine)
    view_lambda = charlie_lambda(view_cosine, alpha)
    integral = 0.0
    for row in range(samples):
        light_cosine = (row + 0.5) / samples
        light_sine = sqrt(1.0 - light_cosine * light_cosine)
        light_lambda = charlie_lambda(light_cosine, alpha)
        normalization = normalize_sheen(view_cosine, light_cosine, roughness)
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
            integral += distribution * visibility * light_cosine / normalization
    return (
        integral * (2.0 * pi / (samples * samples))
    )


def main() -> None:
    shader = (Path(__file__).resolve().parents[1] / "Shaders/PBR/BRDF.hlsli").read_text(encoding="utf-8")
    normalization = re.search(
        r"float CharlieSheenNormalization\(float NoV, float NoL, float perceptualRoughness\)\s*\{([^}]+)\}",
        shader, re.DOTALL)
    if normalization is None or "min(NoV, NoL)" not in normalization.group(1):
        raise SystemExit("Shader Charlie normalization must be symmetric in NoV and NoL")
    match = re.search(r"static const float coefficients\[49\]\s*=\s*\{([^}]+)\};", shader, re.DOTALL)
    if match is None:
        raise SystemExit("Sheen fit coefficients are missing from BRDF.hlsli")
    shader_coefficients = [float(value) for value in re.findall(
        r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", match.group(1))]
    expected_coefficients = [value for row in FIT_COEFFICIENTS for value in row]
    if len(shader_coefficients) != 49 or any(
            abs(actual - expected) > 1.0e-9 for actual, expected in
            zip(shader_coefficients, expected_coefficients)):
        raise SystemExit("Shader and numeric-reference sheen fit coefficients differ")

    roughness_values = (0.045, 0.06, 0.08, 0.1, 0.15, 0.2, 0.3,
                        0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0)
    view_cosines = (0.001, 0.002, 0.005, 0.01, 0.02, 0.03, 0.05,
                    0.07, 0.1, 0.15, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7,
                    0.8, 0.9, 1.0)
    reference_points = [(view_cosine, roughness)
                        for roughness in roughness_values
                        for view_cosine in view_cosines]
    reference_points.extend(
        ((view_cosines[view_index] + view_cosines[view_index + 1]) * 0.5,
         (roughness_values[roughness_index] + roughness_values[roughness_index + 1]) * 0.5)
        for roughness_index in range(len(roughness_values) - 1)
        for view_index in range(len(view_cosines) - 1))
    maximum_albedo = 0.0
    maximum_bound_excess = 0.0
    maximum_fit_error = 0.0
    maximum_layer_energy = 0.0
    for view_cosine, roughness in reference_points:
        albedo = directional_albedo(view_cosine, roughness)
        fitted = fitted_directional_albedo(view_cosine, roughness)
        maximum_albedo = max(maximum_albedo, albedo)
        maximum_bound_excess = max(
            maximum_bound_excess,
            albedo - energy_estimate(view_cosine, roughness),
        )
        maximum_fit_error = max(maximum_fit_error, abs(albedo - fitted))
        maximum_layer_energy = max(maximum_layer_energy, 1.0 - fitted + albedo)

    maximum_reciprocity_error = 0.0
    for roughness in roughness_values:
        for view_cosine in view_cosines:
            for light_cosine in (0.005, 0.03, 0.15, 0.5, 0.9):
                for phase in (0.0, 0.7, 1.8):
                    forward = sheen_brdf(view_cosine, light_cosine, phase, roughness)
                    reverse = sheen_brdf(light_cosine, view_cosine, phase, roughness)
                    maximum_reciprocity_error = max(maximum_reciprocity_error,
                        abs(forward - reverse) / max(1.0, abs(forward), abs(reverse)))

    print(f"Charlie sheen: {len(reference_points)} cases; "
          f"max directional albedo={maximum_albedo:.6f}; "
          f"max estimate excess={maximum_bound_excess:.6f}; "
          f"max fit error={maximum_fit_error:.6f}; "
          f"max layered energy={maximum_layer_energy:.6f}; "
          f"max reciprocity error={maximum_reciprocity_error:.3g}")
    if (maximum_albedo > 1.01 or maximum_bound_excess > 0.01 or
            maximum_fit_error > 0.08 or maximum_layer_energy > 1.03 or
            maximum_reciprocity_error > 1.0e-10):
        raise SystemExit("Sheen numeric contract exceeded its measured bounds")


if __name__ == "__main__":
    main()
