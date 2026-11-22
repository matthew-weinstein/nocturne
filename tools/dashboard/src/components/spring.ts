/** A damped spring with unit mass, stepped by the frame's elapsed seconds. */
export class Spring {
  value = 0;
  target = 0;
  private velocity = 0;
  private stiffness: number;
  private damping: number;

  constructor(stiffness: number, damping: number) {
    this.stiffness = stiffness;
    this.damping = damping;
  }

  jump(value: number): void {
    this.value = value;
    this.target = value;
    this.velocity = 0;
  }

  step(seconds: number): void {
    const substeps = Math.max(1, Math.ceil(seconds / 0.004));
    const dt = seconds / substeps;
    for (let index = 0; index < substeps; index++) {
      const force =
        -this.stiffness * (this.value - this.target) -
        this.damping * this.velocity;
      this.velocity += force * dt;
      this.value += this.velocity * dt;
    }
  }
}
