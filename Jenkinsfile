def IMAGE_TAG = "ncs-toolchain:1.06"
def REPO_CI_TOOLS = "https://github.com/zephyrproject-rtos/ci-tools.git"

pipeline {
  agent {
    docker {
      image "$IMAGE_TAG"
      label "docker && ncs"
    }
  }
  options {
    // Checkout the repository to this folder instead of root
    checkoutToSubdirectory('mcuboot')
  }

  environment {
      // This token is used to by check_compliance to comment on PRs and use checks
      GH_TOKEN = credentials('nordicbuilder-compliance-token')
      COMPLIANCE_ARGS = "-g -r NordicPlayground/fw-nrfconnect-mcuboot -p $CHANGE_ID -S $GIT_COMMIT"
  }

  stages {
    stage('Checkout repositories') {
      steps {
        dir("ci-tools") {
          git branch: "master", url: "$REPO_CI_TOOLS"
        }
      }
    }

    stage('Run compliance check') {
      steps {
        dir('mcuboot') {
          script {
            // If we're a pull request, compare the target branch against the current HEAD (the PR)
            if (env.CHANGE_TARGET) {
              COMMIT_RANGE = "origin/${env.CHANGE_TARGET}..HEAD"
            }
            // If not a PR, it's a non-PR-branch or master build. Compare against the origin.
            else {
              COMMIT_RANGE = "origin/${env.BRANCH_NAME}..HEAD"
            }
            // Run the compliance check
            try {
              sh "../ci-tools/scripts/check_compliance.py $COMPLIANCE_ARGS --commits $COMMIT_RANGE"
            }
            finally {
              junit 'compliance.xml'
              archiveArtifacts artifacts: 'compliance.xml'
            }
          }
        }
      }
    }
  }

  post {
    always {
      // Clean up the working space at the end (including tracked files)
      cleanWs()
    }
  }
}
