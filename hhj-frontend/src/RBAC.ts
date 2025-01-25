export enum Roles { PUBLIC, USER, ADMIN }
export enum Operations { TIMER_START, TIMER_STOP, TIMER_RESET }

class RBAC {
  static instance: RBAC

  private constructor() { }

  can(role: Roles, operation: Operations) {
    return true
  }

  public static getInstance() {
    if (!this.instance) {
      this.instance = new RBAC()
    }
    return this.instance
  }
}

export default RBAC.getInstance()
