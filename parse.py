def parse():
    from argparse import ArgumentParser,ArgumentDefaultsHelpFormatter,Action,Namespace
    from math import pi,sqrt
    from sys import argv

    class PkdParser(ArgumentParser):
        def __init__(self, **kwargs):
            super(PkdParser, self).__init__(**kwargs,
                formatter_class=lambda prog: ArgumentDefaultsHelpFormatter(prog,max_help_position=50, width=132))
            self._specified = Namespace()
        def setSpecified(self,dest):
            setattr(self._specified,dest,True)
        def getSpecified(self):
            return self._specified

    def add_bool(self,name,default=None,**kwargs):
        class BoolAction(Action):
            def __init__(self, option_strings, dest, nargs=None, **kwargs):
                if nargs is not None: raise ValueError("nargs not allowed")
                super(BoolAction, self).__init__(option_strings, dest, nargs=0, **kwargs)
            def __call__(self, parser, namespace, values, option_string=None):
                parser.setSpecified(self.dest)
                setattr(namespace, self.dest, True if option_string[0:1]=='+' else False)
        if default:
            return self.add_argument('-'+name,'+'+name,action=BoolAction,default=default,**kwargs)
        else:
            return self.add_argument('+'+name,'-'+name,action=BoolAction,default=default,**kwargs)

    def add_flag(self,name,default=None,**kwargs):
        class FlagAction(Action):
            def __init__(self, option_strings, dest, **kwargs):
                super(FlagAction, self).__init__(option_strings, dest, **kwargs)
            def __call__(self, parser, namespace, values, option_string=None):
                parser.setSpecified(self.dest)
                setattr(namespace, self.dest, values)
        return self.add_argument('-'+name,default=default,action=FlagAction,**kwargs)


    parser = PkdParser(description='PKDGRAV3 n-body code',prefix_chars='-+',add_help=False)
    parser.add_argument('--help',action='help',help='show this help message and exit')

    gparm = parser.add_argument_group('Gravity, Domains, Trees')
    add_flag(gparm,'e',default=0.0,dest='dSoft',type=float,help='gravitational softening length')
    add_flag(gparm,'eMax',default=0.0,dest='dSoftMax',type=float,help='maximum comoving gravitational softening length (abs or multiplier')
    add_bool(gparm,'PhysSoft',default=False,dest='bPhysicalSoft',help='Physical gravitational softening length')
    add_bool(gparm,'SMM',default=True,dest='bSoftMaxMul',help='Use maximum comoving gravitational softening length as a multiplier')
    add_bool(gparm,'g',default=True,dest='bDoGravity',help='enable interparticle gravity')
    add_flag(gparm,'lingrid',default=0, dest='nGridLin', type=int, help='Grid size for linear species 0=disabled')
    add_bool(gparm,'linpk',default=False,dest='bDoLinPkOutput',help='enable power spectrum output for linear species')
    add_bool(gparm,'2tree',default=False,dest='bDualTree',help='enable second tree for active rungs')
    add_flag(gparm,'fndt',default=0.05,dest='dFracDualTree',type=float,help='Fraction of Active Particles for to use a dual tree')
    add_flag(gparm,'fndd',default=0.1,dest='dFracNoDomainDecomp',type=float,help='Fraction of Active Particles for no DD')
    add_flag(gparm,'fndrf',default=0.1,dest='dFracNoDomainRootFind',type=float,help='Fraction of Active Particles for no DD root finding')
    add_flag(gparm,'fnddc',default=0.1,dest='dFracNoDomainDimChoice',type=float,help='Fraction of Active Particles for no DD dimension choice')

    aparm = parser.add_argument_group('Analysis')
    add_flag(aparm,'npk',default=None,dest='nBinsPk',type=int, help='Number of log bins for P(k)')
    add_flag(aparm,'pk',default=0,dest='nGridPk',type=int, help='Grid size for measure P(k) 0=disabled')
    add_bool(aparm,'pkinterlace',default=True,dest='bPkInterlace',help='Use interlacing to measure P(k)')
    add_flag(aparm,'pko',default=4,dest='iPkOrder',type=int, help='Mass assignment order for measuring P(k)')
    add_bool(aparm,'groupfinder',default=False,dest='bFindGroups',help='enable group finder')
    add_bool(aparm,'hop',default=False,dest='bFindHopGroups',help='enable phase-space group finder')
    add_flag(aparm,'hoptau',default=-4.0,dest='dHopTau',type=float, help='linking length for Gasshopper (negative for multiples of softening)')
    add_flag(aparm,'nMinMembers',default=10, dest='nMinMembers',type=int, help='minimum number of group members')
    add_flag(aparm,'tau',default=0.164,dest='dTau',type=float,help='linking length for FOF in units of mean particle separation')
    add_flag(aparm,'dEnv0',default=-1.0,dest='dEnvironment0',type=float, help='first radius for density environment about a group')
    add_flag(aparm,'dEnv1',default=-1.0,dest='dEnvironment1',type=float, help='second radius for density environment about a group')
    add_bool(aparm,'lc',default=False,dest='bLightCone',help='output light cone data')
    add_flag(aparm,'zlcp',default=0,dest='dRedshiftLCP',type=float, help='starting redshift to output light cone particles')
    add_flag(aparm,'zdel',default=0,dest='dDeltakRedshift',type=float, help='starting redshift to output delta(k) field')
    add_flag(aparm,'healpix',default=8192,dest='nSideHealpix',type=int, help='Number per side of the healpix map')
    add_bool(aparm,'lcp',default=False,dest='bLightConeParticles',help='output light cone particles')

    ioparm = parser.add_argument_group('I/O Parameters')
    add_flag(ioparm,'I', dest='achInFile', help='input file name')
    add_flag(ioparm,'o', default='pkdgrav3', dest='achOutName', help='output name for snapshots and logfile')
    add_flag(ioparm,'op', default='', dest='achOutPath', help='output path for snapshots and logfile')
    add_flag(ioparm,'iop', default='', dest='achIoPath', help='output path for snapshots and logfile')
    add_flag(ioparm,'cpp', default='', dest='achCheckpointPath', help='output path for checkpoints')
    add_flag(ioparm,'dsp', default='', dest='achDataSubPath', help='sub-path for data')
    add_bool(ioparm,'lcin',default=False,dest='bInFileLC',help='input light cone data')
    add_bool(ioparm,'par',default=True,dest='bParaRead',help='enable parallel reading of files')
    add_bool(ioparm,'paw',default=False,dest='bParaWrite',help='disable parallel writing of files')
    add_flag(ioparm,'npar',default=0,dest='nParaRead',type=int, help='number of threads to read with during parallel read (0=unlimited)')
    add_flag(ioparm,'npaw',default=0,dest='nParaWrite',type=int, help='number of threads to write with during parallel write (0=unlimited)')
    add_flag(ioparm,'oi',default=0,dest='iOutInterval',type=int, help='number of timesteps between snapshots')
    add_flag(ioparm,'fof',default=0,dest='iFofInterval',type=int, help='number of timesteps between fof group finding')
    add_flag(ioparm,'ci',default=0,dest='iCheckInterval', type=int, help='number of timesteps between checkpoints')
    add_flag(ioparm,'ol',default=1,dest='iLogInterval',type=int, help='number of timesteps between logfile outputs')
    add_flag(ioparm,'opk',default=1,dest='iPkInterval',type=int, help='number of timesteps between pk outputs')
    add_flag(ioparm,'odk',default=0,dest='iDeltakInterval', type=int, help='number of timesteps between DeltaK outputs')
    add_bool(ioparm,'hdf5',default=False,dest='bHDF5', help='output in HDF5 format')
    add_bool(ioparm,'dp',default=False,dest='bDoublePos', help='input/output double precision positions (standard format only)')
    add_bool(ioparm,'dv',default=False,dest='bDoubleVel', help='input/output double precision velocities (standard format only)')
    add_bool(ioparm,'softout',default=False,dest='bDoSoftOutput',help='softout","enable soft outputs')
    add_bool(ioparm,'den',default=True,dest='bDoDensity',help='enable density outputs')
    add_bool(ioparm,'accout',default=False,dest='bDoAccOutput',help='enable acceleration outputs')
    add_bool(ioparm,'potout',default=False,dest='bDoPotOutput',help='enable potential outputs')
    add_bool(ioparm,'rungout',default=False,dest='bDoRungOutput',help='enable rung outputs')
    add_bool(ioparm,'rungdestout',default=False,dest='bDoRungDestOutput',help='enable rung destination outputs')
    add_bool(ioparm,'std',default=True,dest='bStandard',help='output in standard TIPSY binary format')
    add_flag(ioparm,'compress',default=0, dest='iCompress',type=int, help='compression format, 0=none, 1=gzip, 2=bzip2')
    add_flag(ioparm,'wall',default=0, dest='iWallRunTime',type=int, help='Maximum Wallclock time (in minutes) to run')
    add_flag(ioparm,'signal',default=0, dest='iSignalSeconds',type=int, help='Time (in seconds) that USR1 is sent before termination')
    add_bool(ioparm,'rtrace',default=False,dest='bTraceRelaxation',help='enable relaxation tracing')
    add_bool(ioparm,'restart',default=False,dest='bRestart',help='restart from checkpoint')
    ioparm.add_argument('-orbit', dest='lstOrbits', type=int, action='append', help='Particle ID of particle to write to orbit file (repeatable)')

    tsparm = parser.add_argument_group('Time Stepping')
    add_flag(tsparm,'nstart',default=0,dest='iStartStep',type=int,help='initial step numbering')
    add_flag(tsparm,'n',default=0,dest='nSteps',type=int,help='number of timesteps')
    add_flag(tsparm,'n10',default=0,dest='nSteps10',type=int,help='number of timesteps to z=10')
    add_flag(tsparm,'zto',default=0.0, dest='dRedTo', type=float, help='specifies final redshift for the simulation')
    add_flag(tsparm,'dt',default=0.0, dest='dDelta', type=float, help='time step')
    add_flag(tsparm,'eta',default=0.2, dest='dEta', type=float, help='time step criterion')
    add_bool(tsparm,'gs',default=False,dest='bGravStep',help='Gravity timestepping according to iTimeStep Criterion')
    add_bool(tsparm,'ea',default=False,dest='bEpsAccStep',help='Sqrt(Epsilon on a) timestepping')
    add_bool(tsparm,'isrho',default=False,dest='bDensityStep',help='Sqrt(1/Rho) timestepping')
    add_flag(tsparm,'tsc',default=0, dest='iTimeStepCrit', type=int, help='Criteria for dynamical time-stepping')
    add_flag(tsparm,'nprholoc',default=32, dest='nPartRhoLoc', type=int, help='Number of particles for local density in dynamical time-stepping')
    add_flag(tsparm,'dprefacrholoc',default=pi*4.0/3.0, dest='dPreFacRhoLoc', type=float, help='Pre-factor for local density in dynamical time-stepping')
    add_flag(tsparm,'deccfacmax',default=3000, dest='dEccFacMax', type=float, help='Maximum correction factor for eccentricity correction')
    add_flag(tsparm,'npcoll',default=0, dest='nPartColl', type=int, help='Number of particles in collisional regime')
    add_flag(tsparm,'mrung',default=None, dest='iMaxRung',type=int, help='maximum timestep rung')
    add_bool(tsparm,'NewKDK',default=False,dest='bNewKDK',help='Use new implementation of KDK time stepping=no')
    add_flag(tsparm,'nTR',default=0, dest='nTruncateRung',type=int, help='number of MaxRung particles to delete MaxRung')
    add_flag(tsparm,'nvactrung',default=None, dest='nRungVeryActive',type=int, help='timestep rung to use very active timestepping')
    add_flag(tsparm,'nvactpart',default=0, dest='nPartVeryActive',type=int, help='number of particles to use very active timestepping')

    forcep = parser.add_argument_group('Force Accuracy')
    add_flag(forcep,'theta',default=0.7, dest='dTheta', type=float, help='Barnes opening criterion')
    add_flag(forcep,'theta20',default=None, dest='dTheta20', type=float, help='Barnes opening criterion for 2 < z <= 20')
    add_flag(forcep,'theta2',default=None, dest='dTheta2', type=float, help='Barnes opening criterion for z <= 2')

    periodp = parser.add_argument_group('Periodic Boundaries')
    add_bool(periodp,'cm',default=False,dest='bComove',help='enable comoving coordinates')
    add_bool(periodp,'p',default=False,dest='bPeriodic',help='periodic/non-periodic')
    add_bool(periodp,'ewald',default=True,dest='bEwald',help='enable Ewald correction')
    add_flag(periodp,'ewo',default=4,dest='iEwOrder',type=int,help='Ewald multipole expansion order: 1, 2, 3 or 4')
    add_flag(periodp,'nrep',default=None,dest='nReplicas',type=int,help='nReplicas')
    add_flag(periodp,'L',default=1.0, dest='dPeriod', type=float, help='periodic box length')
    add_flag(periodp,'Lx',default=1.0, dest='dxPeriod', type=float, help='periodic box length in x-dimension')
    add_flag(periodp,'Ly',default=1.0, dest='dyPeriod', type=float, help='periodic box length in y-dimension')
    add_flag(periodp,'Lz',default=1.0, dest='dzPeriod', type=float, help='periodic box length in z-dimension')
    add_flag(periodp,'ew',default=2.6, dest='dEwCut', type=float, help='dEwCut')
    add_flag(periodp,'ewh',default=2.8, dest='dEwhCut', type=float, help='dEwhCut')

    #     /* IC Generation */
    cosmop = parser.add_argument_group('Cosmology')
    add_flag(cosmop,'Hub',default=sqrt(pi*8/3), dest='dHubble0', type=float, help='dHubble0')
    add_flag(cosmop,'Om',default=1.0, dest='dOmega0', type=float, help='dOmega0')
    add_flag(cosmop,'Lambda',default=0, dest='dLambda', type=float, help='dLambda')
    add_flag(cosmop,'omDE',default=0, dest='dOmegaDE', type=float, help='Omega for Dark Energy using w0 and wa parameters: <dOmegaDE')
    add_flag(cosmop,'w0',default=-1, dest='w0', type=float, help='w0 parameter for Dark Energy <w0')
    add_flag(cosmop,'wa',default=0.0, dest='wa', type=float, help='wa parameter for Dark Energy <wa')
    add_flag(cosmop,'Omrad',default=0.0, dest='dOmegaRad', type=float, help='dOmegaRad')
    add_flag(cosmop,'Omb',default=0.0, dest='dOmegab', type=float, help='dOmegab')
    norm = cosmop.add_mutually_exclusive_group()
    add_flag(norm,'S8',default=0.0, dest='dSigma8', type=float, help='dSimga8')
    add_flag(norm,'As',default=0.0, dest='dNormalization', type=float, help='dNormalization')
    add_flag(cosmop,'ns',default=0.0, dest='dSpectral', type=float, help='dSpectral')
    add_flag(cosmop,'alphas',default=0.0, dest='dRunning', type=float, help='Primordial tilt running: <dRunning')
    add_flag(cosmop,'kpivot',default=0.05, dest='dPivot', type=float, help='Primordial pivot scale in 1/Mpc (not h/Mpc): <dPivot')
    add_bool(cosmop,'class',default=False,dest='bClass',help='Enable/disable the use of CLASS')
    add_flag(cosmop,'class_filename', default=None, dest='achClassFilename', help='Name of hdf5 file containing the CLASS data')
    add_flag(cosmop,'lin_species', default=None, dest='achLinSpecies', help='plus-separated string of linear species, e.g. \"ncdm[0]+g+metric\"')
    add_flag(cosmop,'pk_species', default=None, dest='achPkSpecies', help='plus-separated string of species for P(k)')
    add_flag(cosmop,'h',default=0.0, dest='h', type=float, help='hubble parameter h')
    add_flag(cosmop,'mpc',default=1.0, dest='dBoxSize', type=float, help='Simulation Box size in Mpc')
    add_flag(cosmop,'tf', dest='achTfFile', help='transfer file name (file in CMBFAST format)')

    icparm = parser.add_argument_group('Initial Conditions')
    add_flag(icparm,'z',default=None, dest='dRedFrom', type=float, help='specifies initial redshift for the simulation')
    add_flag(icparm,'grid',default=0, dest='nGrid', type=int, help='Grid size for IC 0=disabled')
    add_flag(icparm,'seed',default=0, dest='iSeed', type=int, help='Random seed for IC')
    add_bool(icparm,'2lpt',default=True,dest='b2LPT',help='Enable/disable 2LPT')
    add_bool(icparm,'wic',default=False,dest='bWriteIC',help='Write IC after generating')
    add_bool(icparm,'fixedamp',default=False,dest='bFixedAmpIC',help='Use fixed amplitude of 1 for ICs')
    add_flag(icparm,'fixedphase',default=0.0, dest='dFixedAmpPhasePI', type=float, help='Phase shift for fixed amplitude in units of PI')

    memory = parser.add_argument_group('Memory Model and Control')
    add_flag(memory,'b',default=16,dest='nBucket',type=int,help='max number of particles in a bucket')
    add_flag(memory,'grp',default=64,dest='nGroup',type=int,help='max number of particles in a group')
    add_flag(memory,'extra',default=0.1, dest='dExtraStore', type=float, help='Extra storage for particles')
    add_flag(memory,'treelo',default=14, dest='nTreeBitsLo',type=int, help='number of low bits for tree')
    add_flag(memory,'treehi',default=18, dest='nTreeBitsHi',type=int, help='number of high bits for tree')
    add_bool(memory,'integer',default=False,dest='bMemIntegerPosition', help='Particles have integer positions')
    add_bool(memory,'unordered',default=False,dest='bMemUnordered', help='Particles have no specific order')
    add_bool(memory,'pid',default=False,dest='bMemParticleID', help='Particles have a unique identifier')
    add_bool(memory,'Ma',default=False,dest='bMemAcceleration', help='Particles have acceleration')
    add_bool(memory,'Mv',default=False,dest='bMemVelocity', help='Particles have velocity')
    add_bool(memory,'Mp',default=False,dest='bMemPotential', help='Particles have potential')
    add_bool(memory,'Mg',default=False,dest='bMemGroups', help='Particles support group finding')
    add_bool(memory,'Mm',default=False,dest='bMemMass', help='Particles have individual masses')
    add_bool(memory,'Ms',default=False,dest='bMemSoft', help='Particles have individual softening')
    add_bool(memory,'Mr',default=False,dest='bMemRelaxation', help='Particles have relaxation')
    add_bool(memory,'Mvs',default=False,dest='bMemVelSmooth', help='Particles support velocity smoothing')
    add_bool(memory,'MNm',default=False,dest='bMemNodeMoment', help='Tree nodes support multipole moments')
    add_bool(memory,'MNa',default=False,dest='bMemNodeAcceleration', help='Tree nodes support acceleration (for bGravStep)')
    add_bool(memory,'MNv',default=False,dest='bMemNodeVelocity', help='Tree nodes support velocity (for iTimeStepCrit = 1)')
    add_bool(memory,'MNsph',default=False,dest='bMemNodeSphBounds', help='Tree nodes support fast-gas bounds')
    add_bool(memory,'MNbnd',default=True,dest='bMemNodeBnd', help='Tree nodes support 3D bounds')
    add_bool(memory,'MNvbnd',default=False,dest='bMemNodeVBnd', help='Tree nodes support velocity bounds')

    # /* Gas Parameters */
    gas = parser.add_argument_group('Gas Parameters')
    add_flag(gas,'s',default=64, dest='nSmooth',type=int, help='number of particles to smooth over')
    add_bool(gas,'gas',default=False,dest='bDoGas',help='calculate gas/do not calculate gas')
    add_bool(gas,'GasAdiabatic',default=True,dest='bGasAdiabatic',help='Gas is Adiabatic')
    add_bool(gas,'GasIsothermal',default=False,dest='bGasIsothermal',help='Gas is Isothermal')
    add_bool(gas,'GasCooling',default=False,dest='bGasCooling',help='Gas is Cooling')
    add_bool(gas,'bInitTFromCooling',default=False,dest='bInitTFromCooling',help='set T (also E, Y, etc..) using Cooling initialization value')
    add_flag(gas,'iRungCoolTableUpdate',default=0, dest='iRungCoolTableUpdate', type=int, help='Rung on which to update cool tables')
    add_flag(gas,'etaC',default=0.4, dest='dEtaCourant', type=float, help='Courant criterion')
    add_flag(gas,'etau',default=0.25, dest='dEtaUDot', type=float, help='uDot timestep criterion')
    add_flag(gas,'alpha',default=1.0, dest='dConstAlpha', type=float, help='Alpha constant in viscosity')
    add_flag(gas,'beta',default=2.0, dest='dConstBeta', type=float, help='Beta constant in viscosity')
    add_flag(gas,'gamma',default=5.0/3.0, dest='dConstGamma', type=float, help='Ratio of specific heats')
    add_flag(gas,'mmw',default=1.0, dest='dMeanMolWeight', type=float, help='Mean molecular weight in amu')
    add_flag(gas,'gcnst',default=1.0, dest='dGasConst', type=float, help='Gas Constant')
    add_flag(gas,'kb',default=1.0, dest='dKBoltzUnit', type=float, help='Boltzmann Constant in System Units')
    add_flag(gas,'hmin',default=0.0, dest='dhMinOverSoft', type=float, help='Minimum h as a fraction of Softening')
    add_flag(gas,'metaldiff',default=0.0, dest='dMetalDiffusionCoeff', type=float, help='Coefficient in Metal Diffusion')
    add_flag(gas,'thermaldiff',default=0.0, dest='dThermalDiffusionCoeff', type=float, help='Coefficient in Thermal Diffusion')
    add_flag(gas,'msu',default=1.0, dest='dMsolUnit', type=float, help='Solar mass/system mass unit')
    add_flag(gas,'kpcu',default=1000.0, dest='dKpcUnit', type=float, help='Kiloparsec/system length unit')
    add_flag(gas,'dhonh',default=0.1, dest='ddHonHLimit', type=float, help='|dH|/H Limiter')
    add_flag(gas,'vlim',default=1, dest='iViscosityLimiter', type=int, help='iViscosity Limiter')
    add_flag(gas,'idiff',default=0, dest='iDiffusion', type=int, help='iDiffusion')
    add_bool(gas,'adddel',default=False,dest='bAddDelete',help='Add Delete Particles')
    add_bool(gas,'stfm',default=False,dest='bStarForm',help='Star Forming')
    add_bool(gas,'fdbk',default=False,dest='bFeedback',help='Stars provide feedback')
    add_flag(gas,'stODmin',default=0.0, dest='SFdComovingDenMin', type=float, help='Minimum overdensity for forming stars')
    add_flag(gas,'stPDmin',default=0.1/.76*1.66e-24, dest='SFdPhysDenMin', type=float, help='Minimum physical density for forming stars (gm/cc)')
    add_flag(gas,'ESNPerStarMass',default=5.0276521e+15, dest='SFdESNPerStarMass', type=float, help='ESN per star mass, erg per g of stars')
    add_flag(gas,'SFdTMax',default=1e20, dest='SFdTMax', type=float, help='Maximum temperature for forming stars, K')
    add_flag(gas,'SFdEfficiency',default=0.01, dest='SFdEfficiency', type=float, help='SF Efficiency')
    add_flag(gas,'SFdtCoolingShutoff',default=30e6, dest='SFdtCoolingShutoff', type=float, help='SF Cooling Shutoff duration')
    add_flag(gas,'SFdtFBD',default=10e6, dest='SFdtFeedbackDelay', type=float, help='SF FB delay')
    add_flag(gas,'SFMLPSM',default=0.1, dest='SFdMassLossPerStarMass', type=float, help='SFMSPSM ')
    add_flag(gas,'SFZMPSM',default=0.01, dest='SFdZMassPerStarMass', type=float, help='SF ZMPSM')
    add_flag(gas,'SFISM',default=0.0, dest='SFdInitStarMass', type=float, help='SF ISM')
    add_flag(gas,'SFMGM',default=0.0, dest='SFdMinGasMass', type=float, help='SF MGM')
    add_flag(gas,'SFVFB',default=100, dest='SFdvFB', type=float, help='SF dvFB sound speed in FB region expected, km/s')
    add_bool(gas,'SFbdivv',default=False,dest='SFbdivv',help='SF Use div v for star formation')

    debugp = parser.add_argument_group('Debugging/Testing/Diagnostics')
    add_bool(debugp,'nograv',default=False,dest='bNoGrav', help='enable gravity calulation for testing')
    add_bool(debugp,'dedicated',default=False,dest='bDedicatedMPI', help='enable dedicated MPI thread')
    add_bool(debugp,'sharedmpi',default=False,dest='bSharedMPI', help='enable extra dedicated MPI thread')
    add_bool(debugp,'overwrite',default=False,dest='bOverwrite', help='enable overwrite safety lock')
    add_bool(debugp,'vwarnings',default=True,dest='bVWarnings', help='enable warnings')
    add_bool(debugp,'vstart',default=True,dest='bVStart', help='enable verbose start')
    add_bool(debugp,'vstep',default=True,dest='bVStep', help='enable verbose step')
    add_bool(debugp,'vrungstat',default=True,dest='bVRungStat', help='enable rung statistics')
    add_bool(debugp,'vdetails', default=False,dest='bVDetails', help='enable verbose details')
    add_flag(debugp,'nd',default=5,dest='nDigits', type=int,help='number of digits to use in output filenames')
    add_flag(debugp,'cs',default=0, dest='iCacheSize',type=int, help='size of the MDL cache (0=default)')
    add_flag(debugp,'wqs',default=0, dest='iWorkQueueSize',type=int, help='size of the MDL work queue')
    add_flag(debugp,'cqs',default=8, dest='iCUDAQueueSize',type=int, help='size of the CUDA work queue')

    parser.add_argument('script',nargs='?',default=None,help='File containing parameters or analysis script')

    (params,extra) = parser.parse_known_args()
    spec = parser.getSpecified()
    for k in vars(params):
        if not k in vars(spec): setattr(spec,k,False)
    argv[1:] = extra # Consume the parameters we parsed out
    if params.script is not None: argv[0]=params.script
    return (params,spec)

def update(pars,args,spec):
    for key,value in pars.items():
        if key in vars(args) and not getattr(spec,key):
            setattr(args,key,value)
            setattr(spec,key,True)
        # else: this is a rogue variable?
